file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
execute_process(
    COMMAND "${TUTORIAL}" --small --ply "${PLY_FIXTURE}"
    WORKING_DIRECTORY "${OUTPUT_DIRECTORY}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Tutorial render failed: ${result}\n${output}\n${error}")
endif()
foreach(label IN ITEMS "GPU render [(]HIP events[)]" "Image download [(]wall[)]"
        "Render total [(]wall, launch [+] sync [+] download[)]")
    string(REGEX MATCHALL "${label}: [0-9]+\\.[0-9]+ ms" timings "${output}")
    list(LENGTH timings timing_count)
    if(NOT timing_count EQUAL 1)
        message(FATAL_ERROR "Expected one non-negative frame timing for ${label}.\n${output}")
    endif()
endforeach()
if(NOT output MATCHES "Rendering 3840 x 2160, 1 frame." OR output MATCHES "warmup|mean|median")
    message(FATAL_ERROR "Expected a single render without warmup or repeated measurements.\n${output}")
endif()
if(NOT output MATCHES "Sampling: 1 samples per pixel." OR NOT output MATCHES "Integrator: ray.")
    message(FATAL_ERROR "Expected ray tracing with one sample per pixel by default.\n${output}")
endif()
if(NOT output MATCHES "Material storage: 8 unique materials, 256 palette bytes [+] 32 instance-ID bytes;")
    message(FATAL_ERROR "Expected a shared material palette and per-instance IDs.\n${output}")
endif()
if(NOT output MATCHES "Scene BVH built: 8 instances [(]3 spheres [+] 1 diamond meshes [+] 2 PLY meshes [+] 1 box meshes [+] 1 tori[)], 5 shared geometry BVHs." OR
   NOT output MATCHES "Diamond mesh built: 24 vertices, 8 triangles, 1 geometry BVH." OR
   NOT output MATCHES "Analytic sphere built: 1 AABB, no triangles." OR
   NOT output MATCHES "Box mesh built: 36 vertices, 12 triangles, 1 geometry BVH." OR
   NOT output MATCHES "Analytic torus built:" OR
   NOT output MATCHES "Loaded PLY mesh: .* [(]3 vertices, 1 triangles[)]." OR
   NOT output MATCHES "PLY mesh built: 1 shared geometry BVH.")
    message(FATAL_ERROR "Expected eight instances sharing five geometries.\n${output}")
endif()

set(image "${OUTPUT_DIRECTORY}/scene.png")
file(READ "${image}" header LIMIT 26 HEX)
if(NOT header STREQUAL "89504e470d0a1a0a0000000d4948445200000f00000008700802")
    message(FATAL_ERROR "Expected a 3840 x 2160, 8-bit RGB PNG.")
endif()

file(SHA256 "${image}" default_ray_hash)
execute_process(
    COMMAND "${TUTORIAL}" --small --integrator ray --ply "${PLY_FIXTURE}"
    WORKING_DIRECTORY "${OUTPUT_DIRECTORY}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Explicit ray render failed.\n${output}\n${error}")
endif()
file(SHA256 "${image}" explicit_ray_hash)
if(NOT default_ray_hash STREQUAL explicit_ray_hash)
    message(FATAL_ERROR "Selecting ray explicitly must preserve the default image.")
endif()

execute_process(
    COMMAND "${TUTORIAL}" --small --integrator path --samples 4 --ply "${PLY_FIXTURE}"
    WORKING_DIRECTORY "${OUTPUT_DIRECTORY}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT result STREQUAL "0" OR NOT output MATCHES "Integrator: path." OR
   NOT output MATCHES "Sampling: 4 samples per pixel." OR
   NOT output MATCHES "Rendering 3840 x 2160, 1 frame.")
    message(FATAL_ERROR "Expected selectable single-frame path tracing.\n${output}\n${error}")
endif()
set(path_image "${OUTPUT_DIRECTORY}/scene-path.png")
file(READ "${path_image}" header LIMIT 26 HEX)
file(SHA256 "${path_image}" path_hash)
file(SHA256 "${image}" preserved_ray_hash)
if(NOT header STREQUAL "89504e470d0a1a0a0000000d4948445200000f00000008700802" OR
   path_hash STREQUAL default_ray_hash OR NOT preserved_ray_hash STREQUAL default_ray_hash)
    message(FATAL_ERROR "Path tracing must write a distinct 4K image without replacing the ray image.")
endif()
file(REMOVE "${image}" "${path_image}")

execute_process(
    COMMAND "${TUTORIAL}" --cornell --integrator path --samples 4
    WORKING_DIRECTORY "${OUTPUT_DIRECTORY}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT result STREQUAL "0" OR
   NOT output MATCHES "Area lights: 1." OR
   NOT output MATCHES "Scene BVH built: 8 instances [(]5 walls [+] 1 matte sphere [+] 1 glass sphere [+] 1 area light[)], 2 shared geometry BVHs.")
    message(FATAL_ERROR "Expected a Cornell scene without an external PLY dependency.\n${output}\n${error}")
endif()
set(cornell_image "${OUTPUT_DIRECTORY}/cornell-path.png")
file(READ "${cornell_image}" header LIMIT 26 HEX)
if(NOT header STREQUAL "89504e470d0a1a0a0000000d4948445200000f00000008700802")
    message(FATAL_ERROR "Expected a 3840 x 2160 Cornell PNG.")
endif()
file(REMOVE "${cornell_image}")

execute_process(
    COMMAND "${TUTORIAL}" --dragon --samples 4 --ply "${PLY_FIXTURE}"
    WORKING_DIRECTORY "${OUTPUT_DIRECTORY}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT result STREQUAL "0" OR
   NOT output MATCHES "Scene BVH built: 3 instances [(]1 PLY mesh [+] 1 mirror floor [+] 1 glass sphere[)], 3 geometry BVHs." OR
   NOT output MATCHES "Sampling: 4 samples per pixel." OR
   output MATCHES "Diamond mesh built:|Analytic sphere built:|Analytic torus built:|Box mesh built:")
    message(FATAL_ERROR "Expected a separate dragon scene with a mirror floor and glass sphere.\n${output}\n${error}")
endif()
set(dragon_image "${OUTPUT_DIRECTORY}/dragon.png")
file(READ "${dragon_image}" header LIMIT 26 HEX)
if(NOT header STREQUAL "89504e470d0a1a0a0000000d4948445200000f00000008700802")
    message(FATAL_ERROR "Expected a 3840 x 2160 dragon PNG.")
endif()
file(REMOVE "${dragon_image}")

foreach(samples IN ITEMS 0 -1 nope 2.5 2147483648)
    execute_process(
        COMMAND "${TUTORIAL}" --samples "${samples}"
        RESULT_VARIABLE result
        ERROR_VARIABLE error)
    if(NOT result STREQUAL "1" OR NOT error MATCHES "--samples requires a positive integer.")
        message(FATAL_ERROR "Expected invalid sample count '${samples}' to be rejected.\n${error}")
    endif()
endforeach()
execute_process(
    COMMAND "${TUTORIAL}" --samples
    RESULT_VARIABLE result
    ERROR_VARIABLE error)
if(NOT result STREQUAL "1" OR NOT error MATCHES "--samples requires a positive integer.")
    message(FATAL_ERROR "Expected a missing sample count to be rejected.\n${error}")
endif()

execute_process(
    COMMAND "${TUTORIAL}" --dragon --small
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT result STREQUAL "1" OR NOT error MATCHES "Choose only one of --small, --dragon or --cornell.")
    message(FATAL_ERROR "Expected conflicting scene selections to be rejected.\n${output}\n${error}")
endif()

foreach(other IN ITEMS --small --dragon)
    execute_process(
        COMMAND "${TUTORIAL}" --cornell "${other}"
        RESULT_VARIABLE result
        ERROR_VARIABLE error)
    if(NOT result STREQUAL "1" OR NOT error MATCHES "Choose only one of --small, --dragon or --cornell.")
        message(FATAL_ERROR "Expected conflicting Cornell scene selection to be rejected.\n${error}")
    endif()
endforeach()
foreach(value IN ITEMS nope Path 7)
    execute_process(
        COMMAND "${TUTORIAL}" --integrator "${value}"
        RESULT_VARIABLE result
        ERROR_VARIABLE error)
    if(NOT result STREQUAL "1" OR NOT error MATCHES "--integrator requires ray or path.")
        message(FATAL_ERROR "Expected invalid integrator '${value}' to be rejected.\n${error}")
    endif()
endforeach()
execute_process(
    COMMAND "${TUTORIAL}" --integrator
    RESULT_VARIABLE result
    ERROR_VARIABLE error)
if(NOT result STREQUAL "1" OR NOT error MATCHES "--integrator requires ray or path.")
    message(FATAL_ERROR "Expected a missing integrator to be rejected.\n${error}")
endif()

# A directory at the output filename must be reported as a write failure.
set(blocked "${OUTPUT_DIRECTORY}/blocked")
file(MAKE_DIRECTORY "${blocked}/scene.png")
execute_process(
    COMMAND "${TUTORIAL}" --small --ply "${PLY_FIXTURE}"
    WORKING_DIRECTORY "${blocked}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
file(REMOVE_RECURSE "${blocked}/scene.png")
if(NOT result STREQUAL "1" OR NOT error MATCHES "Cannot open image:")
    message(FATAL_ERROR "Expected an explicit output-file error, got ${result}\n${output}\n${error}")
endif()
