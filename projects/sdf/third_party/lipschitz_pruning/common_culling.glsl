shared ActiveNode s_parent_active_nodes[64];
shared uint16_t s_parent_node_parents[64];

#define INVALID_INDEX 0xffffu



void compute_pruning(vec3 cell_center, vec3 cell_size, int cell_idx) {
    struct StackEntry {
        int idx;
        float d;
    };
    const int STACK_DEPTH = 128;
    StackEntry stack[STACK_DEPTH];
    int stack_idx = 0;

    float R = length(cell_size) * 0.5;

    const int NODESTATE_INACTIVE = 0;
    const int NODESTATE_SKIPPED = 1;
    const int NODESTATE_ACTIVE = 2;

    int parent_cell_idx;
    int parent_offset;
    int num_nodes;
    if (bool(first_lvl)) {
        parent_cell_idx = 0;
        parent_offset = 0;
        num_nodes = total_num_nodes;
    } else {
        parent_cell_idx = int(get_parent_cell_idx(cell_idx, grid_size));
        parent_offset = parent_cells_offset.tab[parent_cell_idx];
        num_nodes = parent_cells_num_active.tab[parent_cell_idx];
    }

    if (num_nodes == 0) {
        num_active_out.tab[cell_idx] = 0;
        cell_value_out.tab[cell_idx] = cell_value_in.tab[parent_cell_idx];
        return;
    }

    if (num_nodes == 1) {
        int cell_offset = atomicAdd(active_count.val, 1);
        num_active_out.tab[cell_idx] = 1;
        child_cells_offset.tab[cell_idx] = cell_offset;
        parents_out.tab[cell_offset] = uint16_t(INVALID_INDEX);
        active_nodes_out.tab[cell_offset] = active_nodes_in.tab[parent_offset];
        cell_value_out.tab[cell_idx] = cell_value_in.tab[parent_cell_idx];
        return;
    }

    int tmp_offset = -1;

    if (subgroupElect()) {
        tmp_offset = atomicAdd(old_to_new_count.val, int(gl_SubgroupSize)*num_nodes);
    }
    tmp_offset = subgroupBroadcastFirst(tmp_offset);

    for (int block = 0; block < (num_nodes+63) / 64; block++) {
        if (block*64+gl_LocalInvocationIndex < num_nodes) {
            s_parent_active_nodes[gl_LocalInvocationIndex] = active_nodes_in.tab[parent_offset + block*64 + gl_LocalInvocationIndex];
        }
        barrier();

        for (int element_idx = 0; element_idx < 64; element_idx++) {
            int i = block*64 + element_idx;
            if (i >= num_nodes) break;

#if 1
            ActiveNode active_node = active_nodes_in.tab[parent_offset + i];
#else
            ActiveNode active_node = s_parent_active_nodes[element_idx];
#endif
            int node_idx = ActiveNode_index(active_node);
            Node node = nodes.tab[node_idx];

            float d;
            if (node.type == NODETYPE_BINARY) {
                StackEntry left_entry = stack[stack_idx-2];
                StackEntry right_entry = stack[stack_idx-1];
                float left_val = left_entry.d;
                float right_val = right_entry.d;
                stack_idx -= 2;

                BinaryOp op = binary_ops.tab[node.idx_in_type];
                float k = BinaryOp_blend_factor(op);
                float s = BinaryOp_sign(op);

                // there are two ways to write the binary operator evaluation
                // the first is how we show in the paper, which doesn't make any additional assumptions on the operators but needs a few additional FLOPS
                // the second relies on the specific form of the operators, which are all min() - kernel() with some signs. This allows us to make a few simplifications
                // compared to the more general form
                // both are strictly equivalent, if you unroll the math they compute the exact same expressions
#if 0
                float c_a = BinaryOp_ca(op);
                float c_b = BinaryOp_cb(op);
                d = BinaryOp_eval(op, c_a*left_val, c_b*right_val, k);
#else
                d = s*(min(s*left_val, s*right_val) - kernel(abs(left_val-right_val), k));
#endif

                int current_state;
                if (abs(left_val - right_val) <= 2 * R + k) {
                    current_state = NODESTATE_ACTIVE;
                } else {
                    current_state = NODESTATE_SKIPPED;
                    if (s*left_val < s*right_val) {
                        Tmp_state_write(tmp.tab[tmp_offset + gl_SubgroupSize*right_entry.idx + gl_SubgroupInvocationID], NODESTATE_INACTIVE);
                    } else {
                        Tmp_state_write(tmp.tab[tmp_offset + gl_SubgroupSize*left_entry.idx + gl_SubgroupInvocationID], NODESTATE_INACTIVE);
                    }
                }

                tmp.tab[tmp_offset + gl_SubgroupSize*i + gl_SubgroupInvocationID] = Tmp(0);
                Tmp_state_write(tmp.tab[tmp_offset + gl_SubgroupSize*i + gl_SubgroupInvocationID], current_state);
                //prim_dist[i] = 1e20;
            } else if (node.type == NODETYPE_PRIMITIVE) {
                Primitive prim = prims.tab[node.idx_in_type];
                d = eval_prim(cell_center, prim);
                tmp.tab[tmp_offset + gl_SubgroupSize*i + gl_SubgroupInvocationID] = Tmp(0);
                Tmp_state_write(tmp.tab[tmp_offset + gl_SubgroupSize*i + gl_SubgroupInvocationID], NODESTATE_ACTIVE);
                //prim_dist[i] = d;
            }

            d *= ActiveNode_sign(active_node) ? 1 : -1;
            StackEntry new_entry;
            new_entry.idx = i;
            new_entry.d = d;
            stack[stack_idx++] = new_entry;
        }
    }

    float d = stack[0].d;
    if (abs(d) > 2*R) {
        num_active_out.tab[cell_idx] = 0;
        cell_value_out.tab[cell_idx] = sign(stack[0].d) * (abs(stack[0].d) - R);
        return;
    }


    int cell_num_active = 0;
    for (int i = num_nodes-1; i >= 0; i--) {

        Tmp tmp_i = tmp.tab[tmp_offset + gl_SubgroupSize*i + gl_SubgroupInvocationID];

        if (Tmp_state_get(tmp_i) == NODESTATE_INACTIVE) {
            Tmp_active_global_write(tmp_i, false);
            Tmp_inactive_ancestors_write(tmp_i, true);
            tmp.tab[tmp_offset + gl_SubgroupSize*i + gl_SubgroupInvocationID] = tmp_i;
        } else {
            uint16_t parent_idx = uint16_t(parents_in.tab[parent_offset+i]);
            Tmp tmp_parent;
            if (parent_idx != uint16_t(INVALID_INDEX)) tmp_parent = tmp.tab[tmp_offset + gl_SubgroupSize*parent_idx + gl_SubgroupInvocationID];
            bool node_has_inactive_ancestors = parent_idx != uint16_t(INVALID_INDEX) ? Tmp_inactive_ancestors_get(tmp_parent) : false;
            int local_state = Tmp_state_get(tmp_i);
            bool node_active_global = (local_state == NODESTATE_ACTIVE) && !node_has_inactive_ancestors;
            if (node_active_global) cell_num_active += 1;


            ActiveNode old_active_node = active_nodes_in.tab[parent_offset+i];
            int node_sign = ActiveNode_sign(old_active_node) ? 1 : -1;
            uint16_t new_parent_idx;
            if (parent_idx != INVALID_INDEX && Tmp_state_get(tmp_parent) == NODESTATE_SKIPPED) {
                node_sign *= Tmp_sign_get(tmp_parent) ? 1 : -1;
                new_parent_idx = Tmp_parent_get(tmp_parent);
            } else {
                new_parent_idx = parent_idx == INVALID_INDEX ? uint16_t(INVALID_INDEX) : uint16_t(parent_idx);
            }

            Tmp_inactive_ancestors_write(tmp_i, node_has_inactive_ancestors);
            Tmp_active_global_write(tmp_i, node_active_global);
            Tmp_parent_write(tmp_i, new_parent_idx);
            Tmp_sign_write(tmp_i, node_sign == 1);
            tmp.tab[tmp_offset + gl_SubgroupSize*i + gl_SubgroupInvocationID] = tmp_i;
        }
    }


    // TODO: warp aggregated atomics
    int cell_offset = atomicAdd(active_count.val, cell_num_active);


    int out_idx = cell_num_active-1;
    for (int i = num_nodes-1; i >= 0; i--) {
        Tmp tmp_i = tmp.tab[tmp_offset + gl_SubgroupSize*i + gl_SubgroupInvocationID];
        if (Tmp_active_global_get(tmp_i)) {
            active_nodes_out.tab[cell_offset + out_idx] = ActiveNode_make(ActiveNode_index(active_nodes_in.tab[parent_offset+i]), Tmp_sign_get(tmp_i));
            old_to_new_scratch.tab[tmp_offset + i*gl_SubgroupSize + gl_SubgroupInvocationID] = uint16_t(out_idx);

            int new_parent_old_idx = Tmp_parent_get(tmp_i);
            uint16_t new_parent_idx = new_parent_old_idx != INVALID_INDEX ? old_to_new_scratch.tab[tmp_offset + gl_SubgroupSize*new_parent_old_idx + gl_SubgroupInvocationID] : uint16_t(INVALID_INDEX);
            parents_out.tab[cell_offset + out_idx] = new_parent_idx;

            out_idx--;
        }
    }

    child_cells_offset.tab[cell_idx] = cell_offset;
    num_active_out.tab[cell_idx] = cell_num_active;

    // TODO: constant for max grid size
    if (out_idx == 1 || grid_size == 256) {
        cell_value_out.tab[cell_idx] = 0;
    }
}