set_languages("c++20")
set_project("black-hole")
set_encodings("utf-8")

add_rules("mode.debug", "mode.release")
add_requires("glfw", "glm", "glew")

target("black-hole")
    set_kind("binary")
    set_rundir(".")
    add_packages("glfw", "glm", "glew")
    add_files("main.cpp")
