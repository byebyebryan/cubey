vec3 cubey_lambert_light(vec3 normal, vec3 light_direction, vec3 ambient_color,
                         vec3 light_color) {
    float diffuse = max(dot(normalize(normal), normalize(light_direction)), 0.0);
    return ambient_color + light_color * diffuse;
}
