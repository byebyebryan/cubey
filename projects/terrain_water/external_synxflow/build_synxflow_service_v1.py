#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build an opt-in, isolated SynxFlow v1.0.1 service-hook extension.

This script accepts only the pinned upstream GitHub tag archive, extracts it
into a fresh private output directory, adds hooks to the flood application and
builds the optional ``flood`` extension there. It never installs packages,
changes global configuration, modifies a Cubey build, or launches a solver.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import platform
import subprocess
import tarfile
from typing import Iterable


UPSTREAM_COMMIT = "8a9781504204a7b41217b483db4190c1f6f340bc"
ARCHIVE_SHA256 = "281e84ac6945b7a10c04e11a226dd1bc56673f2fe4deb03f9095671f3ac5f297"
ARCHIVE_ROOT = "SynxFlow-" + UPSTREAM_COMMIT
REPO_DIR = Path(__file__).resolve().parent
APP_REL = Path("synxflow/apps/cudaFloodSolversPybind")
CU_REL = APP_REL / "cuda_flood_solvers.cu"
BINDING_REL = APP_REL / "flood_solvers_pybind.cc"
DECL_REL = APP_REL / "cuda_flood_solvers.h"
HOOK_REL = APP_REL / "cubey_synxflow_service.h"
RUN_START = "int run(const char* work_dir){"
RUN_END_MARKER = "\n\nScalar dt_out = 0.5;"


class BuildError(RuntimeError):
    """Invalid archive, unsafe input, or unexpected pinned source spelling."""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require_exact(value: str, old: str, new: str, label: str) -> str:
    count = value.count(old)
    if count != 1:
        raise BuildError(f"{label}: expected one pinned spelling, found {count}")
    return value.replace(old, new, 1)


def extract_stock_run(source: str) -> tuple[str, int]:
    if source.count(RUN_START) != 1:
        raise BuildError("flood app does not contain exactly one pinned single-GPU run()")
    start = source.index(RUN_START)
    opening = source.find("{", start)
    if opening < 0:
        raise BuildError("pinned run() opening brace was not found")
    depth = 0
    state = "code"
    index = opening
    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""
        if state == "line_comment":
            if char == "\n":
                state = "code"
        elif state == "block_comment":
            if char == "*" and next_char == "/":
                state = "code"
                index += 1
        elif state in ("string", "character"):
            if char == "\\":
                index += 1
            elif (state == "string" and char == '"') or (state == "character" and char == "'"):
                state = "code"
        elif char == "/" and next_char == "/":
            state = "line_comment"
            index += 1
        elif char == "/" and next_char == "*":
            state = "block_comment"
            index += 1
        elif char == '"':
            state = "string"
        elif char == "'":
            state = "character"
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                end = index + 1
                return source[start:end], end
        index += 1
    raise BuildError("pinned run() closing brace was not found")


def _service_run(stock_run: str) -> str:
    service = require_exact(
        stock_run,
        RUN_START,
        "int run_service(const char* work_dir, py::function hook, bool continuous, bool keep_outputs){",
        "service signature",
    )

    service = require_exact(
        service,
        '  if(cd(work_dir) == -1){\n    printf("The working directory does not exist!/n");\n  }',
        '  if (cd(work_dir) == -1) {\n'
        '    throw std::runtime_error("SynxFlow service work directory does not exist");\n'
        '  }',
        "service working directory",
    )
    service = require_exact(
        service,
        '\t  checkCuda(cudaSetDevice(device_id));',
        '\t  cubey_synxflow_service::check_cuda(cudaSetDevice(device_id), "cudaSetDevice");',
        "service CUDA device selection",
    )
    service = require_exact(
        service,
        '   if (!times_setuo_file) {\n'
        '     std::cout << "Please input current time, total time, output time interval and backup interval" << std::endl;\n'
        '     std::cin >> t_current >> t_all >> dt_out >> backup_interval;\n'
        '   }',
        '   if (!times_setuo_file) {\n'
        '     throw std::runtime_error("SynxFlow service requires input/times_setup.dat");\n'
        '   }',
        "headless time setup",
    )
    service = require_exact(
        service,
        '     while (times_setuo_file >> _time) {\n'
        '       GPU_Time_Values.push_back(_time);\n'
        '     }\n'
        '     t_current = GPU_Time_Values[0];',
        '     while (times_setuo_file >> _time) {\n'
        '       GPU_Time_Values.push_back(_time);\n'
        '     }\n'
        '     if (GPU_Time_Values.size() != 4) {\n'
        '       throw std::runtime_error("SynxFlow service times_setup.dat must contain four values");\n'
        '     }\n'
        '     t_current = GPU_Time_Values[0];',
        "time setup field count",
    )
    service = require_exact(
        service,
        '  //********************************\n'
        '  //*******************Read device setup value from file\n',
        '  if (!std::isfinite(t_current) || t_current < 0.0f ||\n'
        '      !std::isfinite(t_all) || t_all < t_current ||\n'
        '      !std::isfinite(dt_out) || dt_out <= 0.0f ||\n'
        '      !std::isfinite(backup_interval) || backup_interval < 0.0f) {\n'
        '    throw std::runtime_error("SynxFlow service time setup contains invalid values");\n'
        '  }\n'
        '  //********************************\n'
        '  //*******************Read device setup value from file\n',
        "time setup validation",
    )
    service = require_exact(
        service,
        '  cuAdaptiveTimeControl2D time_controller(0.005, t_all, 0.5, t_current);',
        '  if (continuous) {\n'
        '    // Host-only horizon; the adaptive controller and solver math are unchanged.\n'
        '    t_all = std::numeric_limits<Scalar>::max() / 4.0f;\n'
        '  }\n'
        '  cuAdaptiveTimeControl2D time_controller(0.005, t_all, 0.5, t_current);',
        "continuous controller horizon",
    )
    service = require_exact(
        service,
        '  while(backup_time < t_current){\n'
        '    backup_time += backup_interval;\n'
        '  }',
        '  if (backup_interval > 0.0f) {\n'
        '    while(backup_time < t_current){\n'
        '      backup_time += backup_interval;\n'
        '    }\n'
        '  }',
        "zero backup interval guard",
    )
    service = require_exact(
        service,
        '  cuGaugesWriter<Scalar, on_cell> h_writer(fvMeshQueries(mesh), h, "input/field/gauges_pos.dat", "output/h_gauges.dat");\n'
        '  cuGaugesWriter<Scalar, on_cell> eta_writer(fvMeshQueries(mesh), eta, "input/field/gauges_pos.dat", "output/eta_gauges.dat");\n'
        '  cuGaugesWriter<Vector, on_cell> hU_writer(fvMeshQueries(mesh), hU, "input/field/gauges_pos.dat", "output/hU_gauges.dat");',
        '  std::unique_ptr<cuGaugesWriter<Scalar, on_cell> > h_writer;\n'
        '  std::unique_ptr<cuGaugesWriter<Scalar, on_cell> > eta_writer;\n'
        '  std::unique_ptr<cuGaugesWriter<Vector, on_cell> > hU_writer;\n'
        '  if (keep_outputs) {\n'
        '    h_writer.reset(new cuGaugesWriter<Scalar, on_cell>(fvMeshQueries(mesh), h, "input/field/gauges_pos.dat", "output/h_gauges.dat"));\n'
        '    eta_writer.reset(new cuGaugesWriter<Scalar, on_cell>(fvMeshQueries(mesh), eta, "input/field/gauges_pos.dat", "output/eta_gauges.dat"));\n'
        '    hU_writer.reset(new cuGaugesWriter<Vector, on_cell>(fvMeshQueries(mesh), hU, "input/field/gauges_pos.dat", "output/hU_gauges.dat"));\n'
        '  }',
        "conditional gauge writers",
    )
    service = require_exact(
        service,
        '  //write the initial profile\n'
        '  fv::cuUnary(hU, hUx, [] __device__(Vector& a) -> Scalar{ return a.x; });\n'
        '  fv::cuUnary(hU, hUy, [] __device__(Vector& a) -> Scalar{ return a.y; });\n'
        '  raster_writer.write(h, "h", t_out);\n'
        '  raster_writer.write(hUx, "hUx", t_out);\n'
        '  raster_writer.write(hUy, "hUy", t_out);\n'
        '  t_out += dt_out;\n'
        '  \n'
        '  //write initial depth\n'
        '  raster_writer.write(h, "h", time_controller.current());',
        '  //write the initial profile\n'
        '  if (keep_outputs) {\n'
        '    fv::cuUnary(hU, hUx, [] __device__(Vector& a) -> Scalar{ return a.x; });\n'
        '    fv::cuUnary(hU, hUy, [] __device__(Vector& a) -> Scalar{ return a.y; });\n'
        '    raster_writer.write(h, "h", t_out);\n'
        '    raster_writer.write(hUx, "hUx", t_out);\n'
        '    raster_writer.write(hUy, "hUy", t_out);\n'
        '  }\n'
        '  t_out += dt_out;\n'
        '  \n'
        '  //write initial depth\n'
        '  if (keep_outputs) raster_writer.write(h, "h", time_controller.current());',
        "initial output gating",
    )
    service = require_exact(
        service,
        '  std::ofstream fout;\n'
        '  fout.open("output/timestep_log.txt");\n'
        '\n'
        '  double total_runtime = 0.0;\n'
        '  cudaEvent_t start, stop;\n'
        '  cudaEventCreate(&start);\n'
        '  cudaEventCreate(&stop);',
        '  std::ofstream fout;\n'
        '  if (keep_outputs) fout.open("output/timestep_log.txt");\n'
        '\n'
        '  double total_runtime = 0.0;\n'
        '  cubey_synxflow_service::CudaEvents service_events;\n'
        '  cudaEvent_t start = service_events.start();\n'
        '  cudaEvent_t stop = service_events.stop();',
        "conditional log and RAII CUDA events",
    )

    rain_state = ('  bool rain_override_active = false;\n'
                  '  Scalar rain_override_m_per_s = 0.0f;\n')
    service = require_exact(
        service,
        '  h.update_boundary_source("input/field/", "h");\n'
        '  hU.update_boundary_source("input/field/", "hU");\n\n'
        '  //Main loop',
        '  h.update_boundary_source("input/field/", "h");\n'
        '  hU.update_boundary_source("input/field/", "hU");\n\n'
        + rain_state +
        '  cubey_synxflow_service::check_cuda(cudaDeviceSynchronize(), "cudaDeviceSynchronize(initial boundary)");\n'
        '  cubey_synxflow_service::HookResult initial_result =\n'
        '      cubey_synxflow_service::invoke_hook(hook, true, time_controller.current(),\n'
        '          time_controller.dt(), true, h, hU, z);\n'
        '  rain_override_active = initial_result.rain_override_active;\n'
        '  rain_override_m_per_s = initial_result.rain_override_m_per_s;\n'
        '  if (initial_result.status_code != 0) return initial_result.status_code;\n\n'
        '  int consecutive_zero_steps = 0;\n\n'
        '  //Main loop',
        "initial safe-boundary hook",
    )

    service = require_exact(
        service,
        '    precipitation.update_time(time_controller.current(), 0.0);\n'
        '    precipitation.update_data_values();\n\n'
        '    //infiltration, precipitation, sewer sink',
        '    precipitation.update_time(time_controller.current(), 0.0);\n'
        '    precipitation.update_data_values();\n'
        '    if (rain_override_active) {\n'
        '      fv::cuUnaryOn(precipitation, [rain_override_m_per_s] __device__(Scalar& a) -> Scalar{ return rain_override_m_per_s; });\n'
        '    }\n\n'
        '    //infiltration, precipitation, sewer sink',
        "rain overlay after native precipitation update",
    )

    service = require_exact(
        service,
        '    //forwarding the time\n'
        '    time_controller.forward();\n'
        '    time_controller.updateByCFL(gravity, h, hU);',
        '    //forwarding the time\n'
        '    const Scalar previous_step_dt = time_controller.dt();\n'
        '    const Scalar previous_time_s = time_controller.current();\n'
        '    time_controller.forward();\n'
        '    if (!std::isfinite(time_controller.current()) ||\n'
        '        time_controller.current() < previous_time_s ||\n'
        '        (time_controller.current() == previous_time_s && previous_step_dt != 0.0f)) {\n'
        '      throw std::runtime_error("SynxFlow service clock regressed or stalled with positive dt");\n'
        '    }\n'
        '    if (time_controller.current() == previous_time_s) {\n'
        '      ++consecutive_zero_steps;\n'
        '      if (consecutive_zero_steps > 1) {\n'
        '        throw std::runtime_error("SynxFlow service clock repeated its zero-dt boundary pass");\n'
        '      }\n'
        '    } else {\n'
        '      consecutive_zero_steps = 0;\n'
        '    }\n'
        '    time_controller.updateByCFL(gravity, h, hU);\n'
        '    if (!std::isfinite(time_controller.dt()) || time_controller.dt() <= 0.0f) {\n'
        '      throw std::runtime_error("SynxFlow service produced an invalid adaptive timestep");\n'
        '    }',
        "finite adaptive clock guard",
    )
    service = require_exact(
        service,
        '    if (time_controller.current() + time_controller.dt() > t_out){\n'
        '      Scalar dt = t_out - time_controller.current();\n'
        '      time_controller.set_dt(dt);\n'
        '    }',
        '    if (time_controller.current() + time_controller.dt() > t_out){\n'
        '      Scalar dt = t_out - time_controller.current();\n'
        '      time_controller.set_dt(dt);\n'
        '    }\n'
        '    if (!std::isfinite(time_controller.dt()) || time_controller.dt() < 0.0f ||\n'
        '        (time_controller.dt() == 0.0f && time_controller.current() != t_out)) {\n'
        '      throw std::runtime_error("SynxFlow service output cadence produced an invalid timestep");\n'
        '    }',
        "post-output-clamp timestep guard",
    )
    service = require_exact(
        service,
        '    cudaEventRecord(start);',
        '    cubey_synxflow_service::check_cuda(cudaEventRecord(start), "cudaEventRecord(start)");',
        "checked step event start",
    )
    service = require_exact(
        service,
        '    if (cnt % 100 == 0){\n'
        '      h_writer.write(time_controller.current());\n'
        '      eta_writer.write(time_controller.current());\n'
        '\t    hU_writer.write(time_controller.current());\n'
        '    }',
        '    if (keep_outputs && cnt % 100 == 0){\n'
        '      h_writer->write(time_controller.current());\n'
        '      eta_writer->write(time_controller.current());\n'
        '      hU_writer->write(time_controller.current());\n'
        '    }',
        "conditional gauge output",
    )
    service = require_exact(
        service,
        '    py::print(time_controller.current());\n'
        '    fout << time_controller.current() << " " <<time_controller.dt() << std::endl;\n'
        '    cnt++;',
        '    if (keep_outputs) {\n'
        '      py::print(time_controller.current());\n'
        '      fout << time_controller.current() << " " << time_controller.dt() << std::endl;\n'
        '    }\n'
        '    cnt++;',
        "conditional timestep logs",
    )
    service = require_exact(
        service,
        '    cudaEventRecord(stop);\n'
        '    cudaEventSynchronize(stop);\n\n'
        '    float elapsed_time = 0.0;\n'
        '    cudaEventElapsedTime(&elapsed_time, start, stop);\n'
        '    total_runtime += elapsed_time;\n\n'
        '    if (time_controller.current() >= t_out - t_small){\n'
        '      std::cout << "Writing output files" << std::endl;\n'
        '      fv::cuUnary(hU, hUx, [] __device__(Vector& a) -> Scalar{ return a.x; });\n'
        '      fv::cuUnary(hU, hUy, [] __device__(Vector& a) -> Scalar{ return a.y; });\n'
        '      raster_writer.write(h, "h", t_out);\n'
        '      raster_writer.write(hUx, "hUx", t_out);\n'
        '      raster_writer.write(hUy, "hUy", t_out);\n'
        '      t_out += dt_out;\n'
        '    }',
        '    cubey_synxflow_service::check_cuda(cudaEventRecord(stop), "cudaEventRecord(stop)");\n'
        '    cubey_synxflow_service::check_cuda(cudaEventSynchronize(stop), "cudaEventSynchronize(stop)");\n\n'
        '    float elapsed_time = 0.0;\n'
        '    cubey_synxflow_service::check_cuda(cudaEventElapsedTime(&elapsed_time, start, stop), "cudaEventElapsedTime");\n'
        '    total_runtime += elapsed_time;\n'
        '    cubey_synxflow_service::check_cuda(cudaDeviceSynchronize(), "cudaDeviceSynchronize(step boundary)");\n'
        '    const bool output_due = time_controller.current() >= t_out - t_small;\n'
        '    if (time_controller.dt() == 0.0f &&\n'
        '        (!output_due || time_controller.current() != t_out)) {\n'
        '      throw std::runtime_error("zero next timestep is valid only at the exact output boundary");\n'
        '    }\n'
        '    cubey_synxflow_service::HookResult step_result =\n'
        '        cubey_synxflow_service::invoke_hook(hook, false, time_controller.current(),\n'
        '            time_controller.dt(), output_due, h, hU, z);\n'
        '    rain_override_active = step_result.rain_override_active;\n'
        '    rain_override_m_per_s = step_result.rain_override_m_per_s;\n'
        '    if (step_result.status_code != 0) return step_result.status_code;\n\n'
        '    if (output_due){\n'
        '      if (keep_outputs) {\n'
        '        std::cout << "Writing output files" << std::endl;\n'
        '        fv::cuUnary(hU, hUx, [] __device__(Vector& a) -> Scalar{ return a.x; });\n'
        '        fv::cuUnary(hU, hUy, [] __device__(Vector& a) -> Scalar{ return a.y; });\n'
        '        raster_writer.write(h, "h", t_out);\n'
        '        raster_writer.write(hUx, "hUx", t_out);\n'
        '        raster_writer.write(hUy, "hUy", t_out);\n'
        '      }\n'
        '      t_out += dt_out;\n'
        '    }',
        "checked step fence, hook, and output gating",
    )
    service = require_exact(
        service,
        '    if (time_controller.current() >= backup_time - t_small){\n'
        '      std::cout << "Writing backup files" << std::endl;\n'
        '      cuBackupWriter(h, "h_backup_", backup_time);\n'
        '      cuBackupWriter(hU, "hU_backup_", backup_time);\n'
        '      backup_time += backup_interval;\n'
        '    }',
        '    if (time_controller.current() >= backup_time - t_small){\n'
        '      if (keep_outputs) {\n'
        '        std::cout << "Writing backup files" << std::endl;\n'
        '        cuBackupWriter(h, "h_backup_", backup_time);\n'
        '        cuBackupWriter(hU, "hU_backup_", backup_time);\n'
        '      }\n'
        '      backup_time += backup_interval;\n'
        '    }',
        "conditional backup output",
    )
    service = require_exact(
        service,
        '  printf("Writing maximum inundated depth.\\n");\n'
        '  raster_writer.write(h_max, "h_max", t_all);\n'
        '  py::print("Simulation successfully finished!");\n'
        '  std::cout << "Total runtime " << total_runtime << "ms" << std::endl;\n\n'
        '  return 0;',
        '  if (keep_outputs) {\n'
        '    printf("Writing maximum inundated depth.\\n");\n'
        '    raster_writer.write(h_max, "h_max", t_all);\n'
        '    py::print("Simulation successfully finished!");\n'
        '    std::cout << "Total runtime " << total_runtime << "ms" << std::endl;\n'
        '  }\n'
        '  service_events.close();\n\n'
        '  return 0;',
        "conditional completion output and normal event cleanup",
    )

    return service


def verify_operator_body(stock_run: str, service_run: str) -> None:
    start_marker = "    //calculate the surface elevation"
    end_marker = "    //update maximum depth"
    overlay = ('    if (rain_override_active) {\n'
               '      fv::cuUnaryOn(precipitation, [rain_override_m_per_s] __device__(Scalar& a) -> Scalar{ return rain_override_m_per_s; });\n'
               '    }\n')

    def body(value: str) -> str:
        start = value.index(start_marker)
        end = value.index(end_marker, start)
        return value[start:end]

    original = body(stock_run)
    transformed = body(service_run)
    if transformed.count(overlay) != 1:
        raise BuildError("service rain overlay is not located exactly once in the native step")
    transformed = transformed.replace(overlay, "", 1)
    if transformed != original:
        raise BuildError("service changed the pinned numerical operator/source-sink body")


def patch_flood_sources(source_dir: Path) -> dict[str, str]:
    app_dir = source_dir / APP_REL
    cu_path = app_dir / "cuda_flood_solvers.cu"
    binding_path = app_dir / "flood_solvers_pybind.cc"
    decl_path = app_dir / "cuda_flood_solvers.h"

    cu = cu_path.read_text(encoding="utf-8")
    stock_run, end = extract_stock_run(cu)
    service_run = _service_run(stock_run)
    verify_operator_body(stock_run, service_run)
    include_anchor = '#include <pybind11/pybind11.h>\nnamespace py = pybind11;'
    cu = require_exact(
        cu,
        include_anchor,
        '#include <pybind11/pybind11.h>\n#include "cubey_synxflow_service.h"\nnamespace py = pybind11;',
        "app hook include",
    )
    stock_run_after_include, end_after_include = extract_stock_run(cu)
    if stock_run_after_include != stock_run:
        raise BuildError("stock flood.run changed while adding service includes")
    service_text = "\n\n" + service_run
    cu = cu[:end_after_include] + service_text + cu[end_after_include:]
    stock_run_after_append, _ = extract_stock_run(cu)
    if stock_run_after_append != stock_run:
        raise BuildError("generated build does not preserve stock flood.run byte-for-byte")
    cu_path.write_text(cu, encoding="utf-8")

    decl = decl_path.read_text(encoding="utf-8")
    decl = require_exact(
        decl,
        '#define CUDA_FLOOD_SOLVERS_H\n',
        '#define CUDA_FLOOD_SOLVERS_H\n\n#include <pybind11/pybind11.h>\n',
        "service pybind declaration include",
    )
    decl = require_exact(
        decl,
        'int run(const char* work_dir);\n',
        'int run(const char* work_dir);\n'
        'int run_service(const char* work_dir, pybind11::function hook, bool continuous, bool keep_outputs);\n',
        "service declaration",
    )
    decl_path.write_text(decl, encoding="utf-8")

    binding = binding_path.read_text(encoding="utf-8")
    binding = require_exact(
        binding,
        '    m.def("run_mgpus", &run_mgpus, R"pbdoc(',
        '    m.def("run_service", &run_service,\n'
        '          py::arg("work_dir"), py::arg("hook"),\n'
        '          py::arg("continuous") = false, py::arg("keep_outputs") = true,\n'
        '          "Run the optional host-hook service loop; returns 0=complete, 1=reset, 2=stop.");\n\n'
        '    m.def("run_mgpus", &run_mgpus, R"pbdoc(',
        "service Python binding",
    )
    binding_path.write_text(binding, encoding="utf-8")

    hook_path = app_dir / HOOK_REL.name
    if hook_path.exists():
        raise BuildError("pinned archive unexpectedly already contains the Cubey hook header")
    hook_path.write_bytes((REPO_DIR / "cubey_synxflow_service.h").read_bytes())
    return {
        str(CU_REL): sha256(cu_path),
        str(DECL_REL): sha256(decl_path),
        str(BINDING_REL): sha256(binding_path),
        str(HOOK_REL): sha256(hook_path),
    }


def source_file_hashes(source_dir: Path) -> dict[str, str]:
    return {
        path.relative_to(source_dir).as_posix(): sha256(path)
        for path in sorted(source_dir.rglob("*"))
        if path.is_file()
    }


def audit_changed_files(before: dict[str, str], after: dict[str, str]) -> list[str]:
    all_paths = sorted(set(before) | set(after))
    changed = [path for path in all_paths if before.get(path) != after.get(path)]
    expected = sorted({str(CU_REL), str(DECL_REL), str(BINDING_REL), str(HOOK_REL)})
    if changed != expected:
        raise BuildError(f"unexpected changed source paths: {changed!r}")
    if any("/lib/" in f"/{path}" for path in changed):
        raise BuildError("service build may not modify SynxFlow library sources")
    return changed


def _safe_extract(archive_path: Path, destination: Path) -> Path:
    expected_root = ARCHIVE_ROOT
    with tarfile.open(archive_path, mode="r:gz") as archive:
        members = archive.getmembers()
        if not members:
            raise BuildError("pinned source archive is empty")
        for member in members:
            raw = PurePosixPath(member.name)
            if (raw.is_absolute() or ".." in raw.parts or not raw.parts or
                    raw.parts[0] != expected_root or member.issym() or member.islnk() or
                    not (member.isdir() or member.isfile())):
                raise BuildError(f"unsafe or unexpected source archive member: {member.name}")
        archive.extractall(destination)
    source_dir = destination / expected_root
    if not source_dir.is_dir():
        raise BuildError("pinned source archive root is missing")
    return source_dir


def python_build_info(python: Path) -> dict[str, object]:
    code = (
        "import json,sys,sysconfig; "
        "print(json.dumps({'version':list(sys.version_info[:2]),"
        "'include':sysconfig.get_path('include'),"
        "'libdir':sysconfig.get_config_var('LIBDIR'),"
        "'ldlibrary':sysconfig.get_config_var('LDLIBRARY')}))"
    )
    result = json.loads(subprocess.check_output([str(python), "-c", code], text=True))
    if result["version"] != [3, 11]:
        raise BuildError("the pinned extension build requires CPython 3.11")
    return result


def _version(command: list[str]) -> str:
    try:
        return subprocess.check_output(command, text=True, stderr=subprocess.STDOUT).strip()
    except (OSError, subprocess.CalledProcessError) as error:
        raise BuildError(f"could not identify toolchain command {command[0]}") from error


def _out_path(value: Path) -> Path:
    candidate = value.absolute()
    resolved = candidate.resolve(strict=False)
    try:
        resolved.relative_to(REPO_DIR.resolve())
    except ValueError as error:
        raise BuildError("private build output must stay inside external_synxflow/") from error
    if candidate.exists() or candidate.is_symlink():
        raise BuildError("output must be a fresh, non-symlink leaf")
    candidate.parent.mkdir(parents=True, exist_ok=True)
    return candidate


def _configure_command(source: Path, build: Path, cuda: Path,
                       python: Path, python_info: dict[str, object],
                       cc: Path, cxx: Path) -> list[str]:
    return [
        "cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_POLICY_DEFAULT_CMP0146=OLD",
        f"-DCUDA_TOOLKIT_ROOT_DIR={cuda}", f"-DPYTHON_EXECUTABLE={python}",
        f"-DPYTHON_INCLUDE_DIR={python_info['include']}",
        f"-DPYTHON_LIBRARY={Path(str(python_info['libdir'])) / str(python_info['ldlibrary'])}",
        f"-DCMAKE_C_COMPILER={cc}", f"-DCMAKE_CXX_COMPILER={cxx}",
        f"-DCUDA_HOST_COMPILER={cxx}",
    ]


def build(args: argparse.Namespace) -> dict[str, object]:
    archive_path = args.source_archive.resolve(strict=True)
    actual_archive_sha = sha256(archive_path)
    if actual_archive_sha != ARCHIVE_SHA256:
        raise BuildError("source archive SHA-256 does not match the pinned upstream v1.0.1 tag")

    output = _out_path(args.out)
    cuda = args.cuda_root.resolve(strict=True)
    python = args.python.resolve(strict=True)
    cc = args.cc.resolve(strict=True)
    cxx = args.cxx.resolve(strict=True)
    nvcc = cuda / "bin/nvcc"
    if not nvcc.is_file():
        raise BuildError("explicit CUDA root does not contain bin/nvcc")
    if not 1 <= args.jobs <= 32:
        raise BuildError("jobs must be between 1 and 32")
    python_info = python_build_info(python)

    output.mkdir()
    source_dir = _safe_extract(archive_path, output)
    before = source_file_hashes(source_dir)
    patch_flood_sources(source_dir)
    after_patch = source_file_hashes(source_dir)
    changed_paths = audit_changed_files(before, after_patch)

    build_dir = output / "build"
    configure = _configure_command(source_dir, build_dir, cuda, python, python_info, cc, cxx)
    compile_command = ["cmake", "--build", str(build_dir), "--target", "flood", "-j", str(args.jobs)]
    record: dict[str, object] = {
        "schema": "cubey.fluid25d.synxflow-service-build.v1",
        "upstream_tag": "v1.0.1",
        "upstream_commit": UPSTREAM_COMMIT,
        "source_archive": str(archive_path),
        "source_archive_sha256": actual_archive_sha,
        "cuda_root": str(cuda),
        "nvcc_version": _version([str(nvcc), "--version"]),
        "host_c_compiler": str(cc),
        "host_c_compiler_version": _version([str(cc), "--version"]),
        "host_cxx_compiler": str(cxx),
        "host_cxx_compiler_version": _version([str(cxx), "--version"]),
        "python": str(python),
        "python_build_info": python_info,
        "host_platform": platform.platform(),
        "source_dir": str(source_dir),
        "build_dir": str(build_dir),
        "changed_source_paths": changed_paths,
        "changed_file_sha256": {path: after_patch[path] for path in changed_paths},
        "numerical_operator_body_unchanged": True,
        "stock_flood_run_byte_identical": True,
        "configure_command": configure,
        "build_command": compile_command,
        "solver_executed": False,
        "installed": False,
        "passed": False,
    }

    try:
        with (output / "configure.log").open("wb") as log:
            subprocess.run(configure, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=180)
        with (output / "build.log").open("wb") as log:
            subprocess.run(compile_command, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=900)
        after_build = source_file_hashes(source_dir)
        audit_changed_files(before, after_build)
        extension = build_dir / "synxflow/apps/cudaFloodSolversPybind/flood.cpython-311-x86_64-linux-gnu.so"
        if not extension.is_file():
            raise BuildError("CMake reported success but the expected flood extension is missing")
        record.update(
            passed=True,
            extension=str(extension),
            extension_sha256=sha256(extension),
            source_file_count=len(before),
            build_result="compiled only; native solver was not loaded or launched",
        )
    except Exception as error:
        record["failure"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        (output / "build-result.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    return record


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-archive", type=Path, required=True,
                        help="GitHub v1.0.1 source archive matching the fixed SHA-256 pin")
    parser.add_argument("--cuda-root", type=Path, required=True,
                        help="explicit private CUDA 11.7 toolkit root; never installed here")
    parser.add_argument("--python", type=Path, required=True,
                        help="explicit CPython 3.11 executable used for the extension")
    parser.add_argument("--cc", type=Path, required=True)
    parser.add_argument("--cxx", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True,
                        help="fresh private output leaf inside this external_synxflow directory")
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args(argv)
    try:
        result = build(args)
    except (BuildError, OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
