"""Independent double-precision triangle/interface reference for native RT observations."""
import numpy as np


def normalize(value):
    return value / np.linalg.norm(value, axis=-1, keepdims=True)


def interface(incident, outward, inside, ior):
    normal = -outward if inside else outward
    if ior == 1:
        return normalize(incident-2*np.dot(incident,normal)*normal), normalize(incident), 0.0, False
    cos_i = np.clip(-np.dot(incident, normal), 0, 1)
    relative_ior = 1 / ior if inside else ior
    sin_t_squared = max(0, 1 - cos_i * cos_i) / relative_ior**2
    reflected = normalize(incident - 2 * np.dot(incident, normal) * normal)
    if sin_t_squared >= 1:
        return reflected, np.zeros(3), 1.0, True
    cos_t = np.sqrt(1 - sin_t_squared)
    transmitted = normalize((incident + cos_i * normal) / relative_ior - cos_t * normal)
    if ior == 1:
        return reflected, transmitted, 0.0, False
    perpendicular = (cos_i - relative_ior * cos_t) / max(cos_i + relative_ior * cos_t, 1e-20)
    parallel = (relative_ior * cos_i - cos_t) / max(relative_ior * cos_i + cos_t, 1e-20)
    return reflected, transmitted, .5 * (perpendicular**2 + parallel**2), False


def camera(config, width, height):
    yaw, pitch, distance = config['yaw'], config['pitch'], config['distance']
    target = np.array([config['target-x'], config['target-y'], config['target-z']], dtype=float)
    # Native surface-mode orbit policy keeps the camera above the ground.
    if config['environment'] == 'atmosphere':
        pitch = np.clip(pitch, np.arcsin(np.clip((.05-target[1])/distance, -.99, .99)), 1.4)
    eye = target + np.array([np.cos(pitch)*np.sin(yaw), np.sin(pitch), np.cos(pitch)*np.cos(yaw)])*distance
    forward = normalize(target-eye)
    right = normalize(np.cross(forward, [0, 1, 0]))
    up = np.cross(right, forward)
    xx, yy = np.meshgrid((np.arange(width)+.5)/width*2-1, (np.arange(height)+.5)/height*2-1)
    tangent = np.tan(np.deg2rad(config['fov'])/2)
    directions = normalize(forward + tangent*(xx.reshape(-1,1)*right*width/height - yy.reshape(-1,1)*up))
    return eye, directions


def offset(hit, direction):
    epsilon = max(.00002, 2e-6*np.max(np.abs(hit['point'])))
    return hit['point'] + hit['outward'] * (epsilon if np.dot(direction, hit['outward']) >= 0 else -epsilon)


class Scene:
    def __init__(self, data):
        triangles, normals, instance_ids, material_ids, primitive_ids = [], [], [], [], []
        self.materials = data['materials']
        for index, instance in enumerate(data['instances']):
            mesh = data['meshes'][instance['mesh']]
            world = np.array(instance['world'], dtype=float)
            positions = np.array(mesh['positions'], dtype=float)
            positions = (np.column_stack([positions, np.ones(len(positions))]) @ world.T)[:, :3]
            vertex_normals = np.array(mesh['normals']) @ np.linalg.inv(world).T[:3,:3].T
            vertex_normals = normalize(vertex_normals)
            indices = np.array(mesh['indices']).reshape(-1,3)
            triangles.extend(positions[indices])
            normals.extend(vertex_normals[indices])
            instance_ids.extend([index]*len(indices))
            material_ids.extend([instance['material']]*len(indices))
            primitive_ids.extend(range(len(indices)))
        self.triangles = np.array(triangles)
        self.normals = np.array(normals)
        self.instances = np.array(instance_ids)
        self.material = np.array(material_ids)
        self.primitive = np.array(primitive_ids)
        self.a = self.triangles[:,0]
        self.e1 = self.triangles[:,1]-self.a
        self.e2 = self.triangles[:,2]-self.a
        self.outward = normalize(np.cross(self.e1, self.e2))

    def intersect(self, origin, direction, glass_only=False, skip_instance=-1):
        h = np.cross(direction, self.e2)
        determinant = np.einsum('ij,ij->i', self.e1, h)
        valid = np.abs(determinant) > 1e-12
        inverse = np.divide(1, determinant, out=np.zeros_like(determinant), where=valid)
        delta = origin-self.a
        u = np.einsum('ij,ij->i', delta, h)*inverse
        q = np.cross(delta, self.e1)
        v = np.einsum('ij,j->i', q, direction)*inverse
        t = np.einsum('ij,ij->i', self.e2, q)*inverse
        valid &= (u >= 0) & (v >= 0) & (u+v <= 1) & (t >= .000001) & (t <= 100)
        if glass_only:
            valid &= np.array([m['kind'] == 2 for m in self.materials])[self.material]
        valid &= self.instances != skip_instance
        distances = np.where(valid, t, np.inf)
        chosen = distances.argmin()
        if not np.isfinite(distances[chosen]):
            return None
        bary = np.array([1-u[chosen]-v[chosen], u[chosen], v[chosen]])
        outward = self.outward[chosen]
        normal = normalize(bary @ self.normals[chosen])
        geometric = outward if np.dot(outward, direction) < 0 else -outward
        if np.dot(normal, geometric) < 0:
            normal = -normal
        return dict(point=origin+direction*t[chosen], t=t[chosen], outward=outward, normal=normal,
                    bary=bary, instance=int(self.instances[chosen]), material=int(self.material[chosen]),
                    primitive=int(self.primitive[chosen]))

    def shadow(self, hit, light, config):
        origin, visibility, inside = offset(hit, light), np.ones(3), False
        for _ in range(8):
            surface = self.intersect(origin, light)
            if surface is None:
                return np.zeros(3) if inside else visibility
            if inside:
                attenuation = np.array([config['attenuation-red'],config['attenuation-green'],config['attenuation-blue']])
                visibility *= attenuation**(surface['t']/config['attenuation-distance'])
            if self.materials[surface['material']]['kind'] != 2:
                return np.zeros(3)
            _, _, fresnel, _ = interface(light, surface['outward'], inside, config['ior'])
            visibility *= 1-fresnel
            inside = np.dot(light,surface['outward']) < 0
            origin = offset(surface,light)
        return np.zeros(3)
