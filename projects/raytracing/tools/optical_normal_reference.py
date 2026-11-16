"""Independent float64 Q1 interface oracle and angular energy diagnostics.

Directions incoming point toward the hit. G/rawS face the incident medium.
eta is transmitted IOR / incident IOR, matching camera-path radiance mode.
This is an opt-in research module; NumPy is supplied by its executable caller.
"""

import numpy as np

GUARD = 2.0**-16


def unit(v):
    v = np.asarray(v, dtype=float)
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-150)


def tilted(degrees):
    angle = np.deg2rad(np.asarray(degrees, dtype=float))
    return np.stack([np.sin(angle), np.zeros_like(angle), np.cos(angle)], axis=-1)


def fresnel(cosine, eta):
    c = np.clip(np.asarray(cosine), 0, 1)
    eta = np.asarray(eta)
    sine2 = (1 - c * c) / (eta * eta)
    ct = np.sqrt(np.maximum(0, 1 - sine2))
    rs = (c - eta * ct) / np.maximum(c + eta * ct, 1e-150)
    rp = (eta * c - ct) / np.maximum(eta * c + ct, 1e-150)
    return np.where(eta == 1, 0, np.where(sine2 >= 1, 1, (rs * rs + rp * rp) / 2))


def about(incoming, n, eta):
    incoming, n = np.broadcast_arrays(np.asarray(incoming, float), np.asarray(n, float))
    eta = np.broadcast_to(np.asarray(eta, float), incoming.shape[:-1])
    c = np.sum(-incoming * n, axis=-1)
    f = fresnel(c, eta)
    tir = f >= 1
    r = unit(incoming + 2 * c[..., None] * n)
    ct = np.sqrt(np.maximum(0, 1 - (1 - c * c) / (eta * eta)))
    t = unit(incoming / eta[..., None] + (c / eta - ct)[..., None] * n)
    t = np.where(tir[..., None], 0, t)
    t = np.where((eta == 1)[..., None], incoming, t)
    return {"normal": n, "reflected": r, "transmitted": t, "fresnel": f, "tir": tir}


def interface(incoming, geometric, raw_smooth, eta, candidate=True, guard=GUARD):
    incoming, geometric, raw = np.broadcast_arrays(
        np.asarray(incoming, float),
        np.asarray(geometric, float),
        np.asarray(raw_smooth, float),
    )
    eta = np.broadcast_to(np.asarray(eta, float), incoming.shape[:-1])
    baseline = about(incoming, geometric, eta)
    finite = np.isfinite(raw).all(axis=-1)
    length2 = np.sum(raw * raw, axis=-1)
    clean = np.where(finite[..., None], raw, 0)
    smooth_n = unit(clean)
    reason = np.zeros(incoming.shape[:-1], dtype=np.uint32)
    reason |= np.where(~finite, 2, np.where(length2 <= 1e-20, 4, 0)).astype(np.uint32)
    basic = reason == 0
    reason |= np.where(
        basic & (np.sum(smooth_n * geometric, axis=-1) <= guard), 8, 0
    ).astype(np.uint32)
    reason |= np.where(
        basic & (np.sum(-incoming * smooth_n, axis=-1) <= guard), 16, 0
    ).astype(np.uint32)
    valid_incidence = reason == 0
    smooth = about(incoming, smooth_n, eta)
    reason |= np.where(
        valid_incidence
        & (smooth["fresnel"] > 0)
        & (np.sum(smooth["reflected"] * geometric, axis=-1) <= guard),
        32,
        0,
    ).astype(np.uint32)
    reason |= np.where(
        valid_incidence
        & (smooth["fresnel"] < 1)
        & (np.sum(smooth["transmitted"] * geometric, axis=-1) >= -guard),
        64,
        0,
    ).astype(np.uint32)
    if not candidate:
        reason[...] = 1
    reason = np.where(eta == 1, 256, reason).astype(np.uint32)
    use = reason == 0
    result = {
        key: np.where(use[..., None], smooth[key], baseline[key])
        if np.ndim(baseline[key]) == np.ndim(use) + 1
        else np.where(use, smooth[key], baseline[key])
        for key in baseline
    }
    result["reason"] = reason
    return result


def angular_error(a, b):
    # atan2 remains useful for nearly equal directions, unlike acos(dot).
    a, b = unit(a), unit(b)
    return np.rad2deg(
        np.arctan2(np.linalg.norm(np.cross(a, b), axis=-1), np.sum(a * b, axis=-1))
    )


def flux(
    tilt, beam_angle, beam_radius=5, ior=1.5, order=256, candidate=True, guard=GUARD
):
    """Exterior incident beam -> total reflected + transmitted physical flux.

    Integrate the defined camera radiance operator over both outgoing geometric
    hemispheres. Output radiance times projected geometric solid angle is flux
    per unit actual interface area. The exterior source has radiance one inside
    a spherical cap. Its exact incident irradiance is pi*sin(radius)^2*cos(angle).
    Include the existing radiance eta^-2 term on internal outgoing directions.
    This change-of-variables integral includes all preimages, including fallback
    overlaps, without estimating a singular pointwise directional Jacobian.
    """
    if abs(beam_angle) + beam_radius >= 90:
        raise ValueError("beam must remain entirely in the exterior hemisphere")
    nodes, weights = np.polynomial.legendre.leggauss(order)
    z, wz = (nodes + 1) / 2, weights / 2
    phi = (np.arange(order * 4) + 0.5) * (2 * np.pi / (order * 4))
    v = np.stack(
        np.broadcast_arrays(
            np.sqrt(1 - z[:, None] ** 2) * np.cos(phi),
            np.sqrt(1 - z[:, None] ** 2) * np.sin(phi),
            z[:, None],
        ),
        axis=-1,
    )
    measure = wz[:, None] * (2 * np.pi / len(phi)) * z[:, None]
    source = tilted(beam_angle)
    cosine = np.cos(np.deg2rad(beam_radius))
    ng = np.array([0.0, 0.0, 1.0])
    ns = tilted(tilt)
    external = interface(-v, ng, ns, ior, candidate, guard)
    internal = interface(v, -ng, -ns, 1 / ior, candidate, guard)
    reflected = external["fresnel"] * (
        np.sum(external["reflected"] * source, axis=-1) >= cosine
    )
    transmitted = (
        (1 - internal["fresnel"])
        * ior**2
        * (np.sum(internal["transmitted"] * source, axis=-1) >= cosine)
    )
    incident_flux = (
        np.pi * np.sin(np.deg2rad(beam_radius)) ** 2 * np.cos(np.deg2rad(beam_angle))
    )
    r = float(np.sum(measure * reflected) / incident_flux)
    t = float(np.sum(measure * transmitted) / incident_flux)
    return {
        "tilt_degrees": tilt,
        "beam_angle_degrees": beam_angle,
        "beam_radius_degrees": beam_radius,
        "ior": ior,
        "order": order,
        "reflection_ratio": r,
        "transmission_ratio": t,
        "total_ratio": r + t,
        "exact_geometric_ratio": 1.0,
        "method": "two-outgoing-hemisphere projected-solid-angle quadrature",
    }


def flux_preimages(
    tilt, beam_angle, beam_radius=5, ior=1.5, order=128, candidate=True, guard=GUARD
):
    """Integrate in the source cap, analytically enumerating policy preimages.

    On each constant-normal piece reflection has unit solid-angle Jacobian.
    Exterior -> interior Snell has dOmega_inside/dOmega_outside =
    |w.N| / (ior^2 |v.N|). The radiance ior^2 term cancels that factor.
    Check selection independently at each possible geometric/smooth preimage:
    piecewise fallback can create two preimages or no preimage. Sum, do not
    renormalize them. This avoids trying to resolve a small refracted cap on a
    whole-hemisphere grid. The separate outgoing integral remains a crosscheck.
    """
    if abs(beam_angle) + beam_radius >= 90 or ior <= 1:
        raise ValueError(
            "requires an exterior cap wholly above the horizon and IOR > 1"
        )
    nodes, weights = np.polynomial.legendre.leggauss(order)
    rim = np.cos(np.deg2rad(beam_radius))
    z = rim + (nodes + 1) / 2 * (1 - rim)
    phi = (np.arange(4 * order) + 0.5) * (2 * np.pi / (4 * order))
    center = tilted(beam_angle)
    u = np.array([center[2], 0.0, -center[0]])
    sine = np.sqrt(np.maximum(0, 1 - z[:, None] ** 2))
    w = (
        sine[..., None] * np.cos(phi)[..., None] * u
        + sine[..., None] * np.sin(phi)[..., None] * np.array([0.0, 1.0, 0.0])
        + z[:, None, None] * center
    )
    measure = weights[:, None] / 2 * (1 - rim) * (2 * np.pi / len(phi))
    ng = np.array([0.0, 0.0, 1.0])
    ns = tilted(tilt)
    contributions = []
    normals = [(ng, False)]
    if candidate and abs(tilt) > 1e-12:
        normals.append((ns, True))
    for n, smooth_piece in normals:
        vr = about(-w, n, ior)["reflected"]
        pr = interface(-vr, ng, ns, ior, candidate, guard)
        selected_r = (pr["reason"] == 0) if smooth_piece else (pr["reason"] != 0)
        if not candidate or abs(tilt) <= 1e-12:
            selected_r = np.ones(w.shape[:-1], dtype=bool)
        good_r = selected_r & (vr[..., 2] > 0) & (np.sum(w * n, axis=-1) > 0)
        r_flux = np.where(good_r, pr["fresnel"] * vr[..., 2], 0)
        vt = about(-w, n, ior)["transmitted"]
        pt = interface(-vt, -ng, -ns, 1 / ior, candidate, guard)
        selected_t = (pt["reason"] == 0) if smooth_piece else (pt["reason"] != 0)
        if not candidate or abs(tilt) <= 1e-12:
            selected_t = np.ones(w.shape[:-1], dtype=bool)
        wn = np.sum(w * n, axis=-1)
        vn = np.abs(np.sum(vt * n, axis=-1))
        good_t = selected_t & (vt[..., 2] < 0) & (wn > 0) & (vn > 0)
        t_flux = np.where(
            good_t,
            (1 - pt["fresnel"]) * np.abs(vt[..., 2]) * wn / np.maximum(vn, 1e-150),
            0,
        )
        incident_flux = np.pi * np.sin(np.deg2rad(beam_radius)) ** 2 * center[2]
        contributions.append(
            {
                "piece": "smooth" if smooth_piece else "geometric",
                "reflection_ratio": float(np.sum(measure * r_flux) / incident_flux),
                "transmission_ratio": float(np.sum(measure * t_flux) / incident_flux),
                "reflected_preimage_fraction": float(np.mean(good_r)),
                "transmitted_preimage_fraction": float(np.mean(good_t)),
            }
        )
    reflected = sum(c["reflection_ratio"] for c in contributions)
    transmitted = sum(c["transmission_ratio"] for c in contributions)
    return {
        "tilt_degrees": tilt,
        "beam_angle_degrees": beam_angle,
        "beam_radius_degrees": beam_radius,
        "ior": ior,
        "order": order,
        "reflection_ratio": reflected,
        "transmission_ratio": transmitted,
        "total_ratio": reflected + transmitted,
        "pieces": contributions,
        "exact_geometric_ratio": 1.0,
        "method": "source-cap quadrature with all analytic piecewise-normal preimages",
    }
