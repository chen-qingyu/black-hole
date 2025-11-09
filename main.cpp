#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <sstream>
#include <vector>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef _WIN32
extern "C" // Export symbols to request high-performance GPU
{
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

using glm::vec3, glm::vec4, glm::mat4;

// Constants
constexpr float PI = std::numbers::pi_v<float>;
constexpr double C = 299'792'458.0;
constexpr double G = 6.674'30e-11;

// Global state
bool g_gravity = false;

struct Camera
{
    // Center the camera orbit on the black hole at (0, 0, 0)
    const vec3 target = vec3(0.0f); // Always look at the black hole center
    float radius = 1.38e11f;
    float minRadius = 1e10f, maxRadius = 1e12f;

    float azimuth = -2.35f;
    float elevation = 1.5f;

    float orbitSpeed = 0.01f;
    float panSpeed = 0.01f;
    double zoomSpeed = 25e9f;

    bool dragging = false;
    bool panning = false;
    bool moving = false; // For compute shader optimization
    double lastX = 0.0, lastY = 0.0;

    // Calculate camera position in world space
    vec3 position() const
    {
        float clampedElevation = std::clamp(elevation, 0.01f, PI - 0.01f);
        // Orbit around (0,0,0) always
        return vec3(radius * std::sin(clampedElevation) * std::cos(azimuth),
                    radius * std::cos(clampedElevation),
                    radius * std::sin(clampedElevation) * std::sin(azimuth));
    }

    void update()
    {
        moving = dragging || panning;
    }

    void processMouseMove(double x, double y)
    {
        float dx = float(x - lastX);
        float dy = float(y - lastY);

        if (dragging && panning)
        {
            // Pan: Shift + Left or Middle Mouse
            // Disable panning to keep camera centered on black hole
        }
        else if (dragging && !panning)
        {
            // Orbit: Left mouse only
            azimuth += dx * orbitSpeed;
            elevation -= dy * orbitSpeed;
            elevation = std::clamp(elevation, 0.01f, PI - 0.01f);
        }

        lastX = x;
        lastY = y;
        update();
    }

    void processMouseButton(int button, int action, int mods, GLFWwindow* win)
    {
        if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_MIDDLE)
        {
            if (action == GLFW_PRESS)
            {
                dragging = true;
                // Disable panning so camera always orbits center
                panning = false;
                glfwGetCursorPos(win, &lastX, &lastY);
            }
            else if (action == GLFW_RELEASE)
            {
                dragging = false;
                panning = false;
            }
        }
        if (button == GLFW_MOUSE_BUTTON_RIGHT)
        {
            if (action == GLFW_PRESS)
            {
                g_gravity = true;
            }
            else if (action == GLFW_RELEASE)
            {
                g_gravity = false;
            }
        }
    }

    void processScroll(double xoffset, double yoffset)
    {
        radius -= yoffset * zoomSpeed;
        radius = glm::clamp(radius, minRadius, maxRadius);
        update();
    }

    void processKey(int key, int scancode, int action, int mods)
    {
        if (action == GLFW_PRESS && key == GLFW_KEY_G)
        {
            g_gravity = !g_gravity;
            std::cout << "[INFO] Gravity turned " << (g_gravity ? "ON" : "OFF") << '\n';
        }
    }
};

Camera camera;

struct BlackHole
{
    vec3 position;
    double mass;
    double radius;
    double r_s;

    BlackHole(vec3 pos, float m)
        : position(pos)
        , mass(m)
    {
        r_s = 2.0 * G * mass / (C * C);
    }

    bool Intercept(float px, float py, float pz) const
    {
        double dx = double(px) - double(position.x);
        double dy = double(py) - double(position.y);
        double dz = double(pz) - double(position.z);
        double dist2 = dx * dx + dy * dy + dz * dz;
        return dist2 < r_s * r_s;
    }
};

BlackHole SagA(vec3(0.0f), 8.54e36); // Sagittarius A black hole

struct ObjectData
{
    vec4 posRadius; // xyz = position, w = radius
    vec4 color;     // rgb = color, a = unused
    float mass;
    vec3 velocity = vec3(0.0f);
};

std::vector<ObjectData> objects = {
    {vec4(4e11f, 0.0f, 0.0f, 4e10f), vec4(1, 1, 0, 1), 1.98892e30f},
    {vec4(0.0f, 0.0f, 4e11f, 4e10f), vec4(1, 0, 0, 1), 1.98892e30f},
    {vec4(0.0f, 0.0f, 0.0f, static_cast<float>(SagA.r_s)), vec4(0, 0, 0, 1), static_cast<float>(SagA.mass)},
};

struct Engine
{
    struct QuadData
    {
        GLuint vao;
        GLuint texture;
    };

    GLuint gridShaderProgram;
    // -- Quad & Texture render -- //
    GLFWwindow* window;
    GLuint quadVAO;
    GLuint texture;
    GLuint shaderProgram;
    GLuint computeProgram = 0;
    // -- UBOs -- //
    GLuint cameraUBO = 0;
    GLuint diskUBO = 0;
    GLuint objectsUBO = 0;
    // -- grid mess vars -- //
    GLuint gridVAO = 0;
    GLuint gridVBO = 0;
    GLuint gridEBO = 0;
    int gridIndexCount = 0;

    int WIDTH = 800;               // Window width
    int HEIGHT = 600;              // Window height
    int COMPUTE_WIDTH = 200;       // Compute resolution width
    int COMPUTE_HEIGHT = 150;      // Compute resolution height
    float width = 100000000000.0f; // Width of the viewport in meters
    float height = 75000000000.0f; // Height of the viewport in meters

    Engine()
    {
        if (!glfwInit())
        {
            std::cerr << "GLFW init failed\n";
            exit(EXIT_FAILURE);
        }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        window = glfwCreateWindow(WIDTH, HEIGHT, "Black Hole", nullptr, nullptr);
        if (!window)
        {
            std::cerr << "Failed to create GLFW window\n";
            glfwTerminate();
            exit(EXIT_FAILURE);
        }
        glfwMakeContextCurrent(window);
        glewExperimental = GL_TRUE;
        GLenum glewErr = glewInit();
        if (glewErr != GLEW_OK)
        {
            std::cerr << "Failed to initialize GLEW: " << (const char*)glewGetErrorString(glewErr) << "\n";
            glfwTerminate();
            exit(EXIT_FAILURE);
        }
        std::cout << "OpenGL " << glGetString(GL_VERSION) << "\n";
        std::cout << "Using GPU: " << glGetString(GL_RENDERER) << "\n";
        shaderProgram = CreateShaderProgram();
        gridShaderProgram = CreateShaderProgram("grid.vert", "grid.frag");
        computeProgram = CreateComputeProgram("geodesic.comp");
        glGenBuffers(1, &cameraUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
        glBufferData(GL_UNIFORM_BUFFER, 128, nullptr, GL_DYNAMIC_DRAW); // alloc ~128 bytes
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, cameraUBO);              // binding = 1 matches shader

        glGenBuffers(1, &diskUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, diskUBO);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 4, nullptr, GL_DYNAMIC_DRAW); // 3 values + 1 padding
        glBindBufferBase(GL_UNIFORM_BUFFER, 2, diskUBO);                              // binding = 2 matches compute shader

        glGenBuffers(1, &objectsUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, objectsUBO);
        // allocate space for 16 objects:
        // sizeof(int) + padding + 16×(vec4 posRadius + vec4 color)
        GLsizeiptr objUBOSize = sizeof(int) + 3 * sizeof(float) + 16 * (sizeof(vec4) + sizeof(vec4)) + 16 * sizeof(float); // 16 floats for mass
        glBufferData(GL_UNIFORM_BUFFER, objUBOSize, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 3, objectsUBO); // binding = 3 matches shader

        auto [vao, tex] = QuadVAO();
        quadVAO = vao;
        texture = tex;
    }

    void generateGrid(const std::vector<ObjectData>& objects)
    {
        constexpr int gridSize = 25;
        constexpr float spacing = 1e10f;

        std::vector<vec3> vertices;
        std::vector<GLuint> indices;

        for (int z = 0; z <= gridSize; ++z)
        {
            for (int x = 0; x <= gridSize; ++x)
            {
                const float worldX = (x - gridSize / 2) * spacing;
                const float worldZ = (z - gridSize / 2) * spacing;

                float y = 0.0f;

                // Warp grid using Schwarzschild geometry
                for (const auto& obj : objects)
                {
                    const vec3 objPos = vec3(obj.posRadius);
                    const double r_s = 2.0 * G * obj.mass / (C * C);
                    const double dx = worldX - objPos.x;
                    const double dz = worldZ - objPos.z;
                    const double dist = std::sqrt(dx * dx + dz * dz);

                    if (dist > r_s)
                    {
                        const double deltaY = 2.0 * std::sqrt(r_s * (dist - r_s));
                        y += static_cast<float>(deltaY) - 3e10f;
                    }
                    else
                    {
                        y += 2.0f * static_cast<float>(std::sqrt(r_s * r_s)) - 3e10f;
                    }
                }

                vertices.emplace_back(worldX, y, worldZ);
            }
        }

        // Add indices for GL_LINE rendering
        for (int z = 0; z < gridSize; ++z)
        {
            for (int x = 0; x < gridSize; ++x)
            {
                const int i = z * (gridSize + 1) + x;
                indices.push_back(i);
                indices.push_back(i + 1);
                indices.push_back(i);
                indices.push_back(i + gridSize + 1);
            }
        }

        // Upload to GPU
        if (gridVAO == 0)
            glGenVertexArrays(1, &gridVAO);
        if (gridVBO == 0)
            glGenBuffers(1, &gridVBO);
        if (gridEBO == 0)
            glGenBuffers(1, &gridEBO);

        glBindVertexArray(gridVAO);

        glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(vec3), vertices.data(), GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gridEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0); // location = 0
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), (void*)0);

        gridIndexCount = indices.size();

        glBindVertexArray(0);
    }

    void drawGrid(const mat4& viewProj)
    {
        glUseProgram(gridShaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(gridShaderProgram, "viewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
        glBindVertexArray(gridVAO);

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glDrawElements(GL_LINES, gridIndexCount, GL_UNSIGNED_INT, 0);

        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);
    }

    void drawFullScreenQuad()
    {
        glUseProgram(shaderProgram); // fragment + vertex shader
        glBindVertexArray(quadVAO);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(glGetUniformLocation(shaderProgram, "screenTexture"), 0);

        glDisable(GL_DEPTH_TEST);              // draw as background
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 6); // 2 triangles
        glEnable(GL_DEPTH_TEST);
    }

    GLuint CreateShaderProgram()
    {
        const char* vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;  // Changed to vec2
        layout (location = 1) in vec2 aTexCoord;
        out vec2 TexCoord;
        void main() {
            gl_Position = vec4(aPos, 0.0, 1.0);  // Explicit z=0
            TexCoord = aTexCoord;
        })";

        const char* fragmentShaderSource = R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 FragColor;
        uniform sampler2D screenTexture;
        void main() {
            FragColor = texture(screenTexture, TexCoord);
        })";

        // vertex shader
        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
        glCompileShader(vertexShader);

        // fragment shader
        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
        glCompileShader(fragmentShader);

        GLuint shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        return shaderProgram;
    };

    GLuint CreateShaderProgram(const char* vertPath, const char* fragPath)
    {
        auto loadShader = [](const char* path, GLenum type) -> GLuint
        {
            std::ifstream in(path);
            if (!in.is_open())
            {
                std::cerr << "Failed to open shader: " << path << '\n';
                std::exit(EXIT_FAILURE);
            }
            std::stringstream ss;
            ss << in.rdbuf();
            std::string srcStr = ss.str();
            const char* src = srcStr.c_str();

            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &src, nullptr);
            glCompileShader(shader);

            GLint success;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success)
            {
                GLint logLen;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
                std::vector<char> log(logLen);
                glGetShaderInfoLog(shader, logLen, nullptr, log.data());
                std::cerr << "Shader compile error (" << path << "):\n"
                          << log.data() << '\n';
                std::exit(EXIT_FAILURE);
            }
            return shader;
        };

        GLuint vertShader = loadShader(vertPath, GL_VERTEX_SHADER);
        GLuint fragShader = loadShader(fragPath, GL_FRAGMENT_SHADER);

        GLuint program = glCreateProgram();
        glAttachShader(program, vertShader);
        glAttachShader(program, fragShader);
        glLinkProgram(program);

        GLint linkSuccess;
        glGetProgramiv(program, GL_LINK_STATUS, &linkSuccess);
        if (!linkSuccess)
        {
            GLint logLen;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
            std::vector<char> log(logLen);
            glGetProgramInfoLog(program, logLen, nullptr, log.data());
            std::cerr << "Shader link error:\n"
                      << log.data() << '\n';
            std::exit(EXIT_FAILURE);
        }

        glDeleteShader(vertShader);
        glDeleteShader(fragShader);

        return program;
    }

    GLuint CreateComputeProgram(const char* path)
    {
        // 1) read GLSL source
        std::ifstream in(path);
        if (!in.is_open())
        {
            std::cerr << "Failed to open compute shader: " << path << '\n';
            std::exit(EXIT_FAILURE);
        }
        std::stringstream ss;
        ss << in.rdbuf();
        std::string srcStr = ss.str();
        const char* src = srcStr.c_str();

        // 2) compile
        GLuint cs = glCreateShader(GL_COMPUTE_SHADER);
        glShaderSource(cs, 1, &src, nullptr);
        glCompileShader(cs);

        GLint ok;
        glGetShaderiv(cs, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            GLint logLen;
            glGetShaderiv(cs, GL_INFO_LOG_LENGTH, &logLen);
            std::vector<char> log(logLen);
            glGetShaderInfoLog(cs, logLen, nullptr, log.data());
            std::cerr << "Compute shader compile error:\n"
                      << log.data() << '\n';
            std::exit(EXIT_FAILURE);
        }

        // 3) link
        GLuint prog = glCreateProgram();
        glAttachShader(prog, cs);
        glLinkProgram(prog);
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            GLint logLen;
            glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
            std::vector<char> log(logLen);
            glGetProgramInfoLog(prog, logLen, nullptr, log.data());
            std::cerr << "Compute shader link error:\n"
                      << log.data() << '\n';
            std::exit(EXIT_FAILURE);
        }

        glDeleteShader(cs);
        return prog;
    }

    void dispatchCompute(const Camera& cam)
    {
        // determine target compute resolution
        const int cw = cam.moving ? COMPUTE_WIDTH : 200;
        const int ch = cam.moving ? COMPUTE_HEIGHT : 150;

        // 1) reallocate the texture if needed
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cw, ch, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        // 2) bind compute program & UBOs
        glUseProgram(computeProgram);
        uploadCameraUBO(cam);
        uploadDiskUBO();
        uploadObjectsUBO(objects);

        // 3) bind it as image unit 0
        glBindImageTexture(0, texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

        // 4) dispatch grid
        constexpr float workGroupSize = 16.0f;
        const auto groupsX = static_cast<GLuint>(std::ceil(cw / workGroupSize));
        const auto groupsY = static_cast<GLuint>(std::ceil(ch / workGroupSize));
        glDispatchCompute(groupsX, groupsY, 1);

        // 5) sync
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }

    void uploadCameraUBO(const Camera& cam)
    {
        struct UBOData
        {
            vec3 pos;
            float _pad0;
            vec3 right;
            float _pad1;
            vec3 up;
            float _pad2;
            vec3 forward;
            float _pad3;
            float tanHalfFov;
            float aspect;
            bool moving;
            int _pad4;
        } data;

        vec3 fwd = normalize(cam.target - cam.position());
        vec3 up = vec3(0, 1, 0); // y axis is up, so disk is in x-z plane
        vec3 right = normalize(cross(fwd, up));
        up = cross(right, fwd);

        data.pos = cam.position();
        data.right = right;
        data.up = up;
        data.forward = fwd;
        data.tanHalfFov = std::tan(glm::radians(60.0f * 0.5f));
        data.aspect = float(WIDTH) / float(HEIGHT);
        data.moving = cam.dragging || cam.panning;

        glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(UBOData), &data);
    }

    void uploadObjectsUBO(const std::vector<ObjectData>& objs)
    {
        struct UBOData
        {
            int numObjects;
            float _pad0, _pad1, _pad2;
            vec4 posRadius[16];
            vec4 color[16];
            float mass[16];
        } data;

        const auto count = std::min(objs.size(), size_t{16});
        data.numObjects = static_cast<int>(count);

        for (size_t i = 0; i < count; ++i)
        {
            data.posRadius[i] = objs[i].posRadius;
            data.color[i] = objs[i].color;
            data.mass[i] = objs[i].mass;
        }

        glBindBuffer(GL_UNIFORM_BUFFER, objectsUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), &data);
    }

    void uploadDiskUBO()
    {
        struct DiskData
        {
            float innerRadius = SagA.r_s * 2.2f;
            float outerRadius = SagA.r_s * 5.2f;
            float numRays = 2.0f;
            float thickness = 1e9f;
        } diskData;

        glBindBuffer(GL_UNIFORM_BUFFER, diskUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(diskData), &diskData);
    }

    QuadData QuadVAO()
    {
        constexpr float quadVertices[] = {
            // positions   // texCoords
            -1.0f, 1.0f, 0.0f, 1.0f,  // top left
            -1.0f, -1.0f, 0.0f, 0.0f, // bottom left
            1.0f, -1.0f, 1.0f, 0.0f,  // bottom right

            -1.0f, 1.0f, 0.0f, 1.0f, // top left
            1.0f, -1.0f, 1.0f, 0.0f, // bottom right
            1.0f, 1.0f, 1.0f, 1.0f   // top right
        };

        GLuint VAO, VBO;
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, COMPUTE_WIDTH, COMPUTE_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        return {VAO, texture};
    }

    void renderScene()
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(shaderProgram);
        glBindVertexArray(quadVAO);
        // make sure your fragment shader samples from texture unit 0:
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glfwSwapBuffers(window);
        glfwPollEvents();
    };
};

Engine engine;

void setupCameraCallbacks(GLFWwindow* window)
{
    glfwSetWindowUserPointer(window, &camera);

    glfwSetMouseButtonCallback(window, [](GLFWwindow* win, int button, int action, int mods)
                               { auto *cam = static_cast<Camera *>(glfwGetWindowUserPointer(win));
                                 cam->processMouseButton(button, action, mods, win); });

    glfwSetCursorPosCallback(window, [](GLFWwindow* win, double x, double y)
                             { auto *cam = static_cast<Camera *>(glfwGetWindowUserPointer(win));
                               cam->processMouseMove(x, y); });

    glfwSetScrollCallback(window, [](GLFWwindow* win, double xoffset, double yoffset)
                          { auto *cam = static_cast<Camera *>(glfwGetWindowUserPointer(win));
                            cam->processScroll(xoffset, yoffset); });

    glfwSetKeyCallback(window, [](GLFWwindow* win, int key, int scancode, int action, int mods)
                       { auto *cam = static_cast<Camera *>(glfwGetWindowUserPointer(win));
                         cam->processKey(key, scancode, action, mods); });
}

int main()
{
    setupCameraCallbacks(engine.window);

    double lastTime = glfwGetTime();
    double lastPrintTime = lastTime;
    int framesCount = 0;

    while (!glfwWindowShouldClose(engine.window))
    {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        double now = glfwGetTime();
        lastTime = now;

        // Update FPS and camera info
        ++framesCount;
        if (now - lastPrintTime >= 0.2)
        {
            double fps = framesCount / (now - lastPrintTime);
            std::cout << std::format("\rFPS: {:.1f} | Radius: {:.2e} | Azimuth: {:.2f} | Elevation: {:.2f}",
                                     fps, camera.radius, camera.azimuth, camera.elevation);
            framesCount = 0;
            lastPrintTime = now;
        }

        // Gravity simulation
        if (g_gravity)
        {
            for (auto& obj : objects)
            {
                vec3 totalAcc(0.0f);
                for (const auto& obj2 : objects)
                {
                    if (&obj == &obj2)
                        continue;

                    vec3 delta = vec3(obj2.posRadius) - vec3(obj.posRadius);
                    float distance = glm::length(delta);

                    if (distance > 0.0f)
                    {
                        vec3 direction = delta / distance;
                        double force = (G * obj.mass * obj2.mass) / (distance * distance);
                        totalAcc += direction * float(force / obj.mass);
                    }
                }

                obj.velocity += totalAcc;
                obj.posRadius += vec4(obj.velocity, 0.0f);
            }
        }

        // ---------- GRID ------------- //
        // 2) rebuild grid mesh on CPU
        engine.generateGrid(objects);
        // 5) overlay the bent grid
        mat4 view = glm::lookAt(camera.position(), camera.target, vec3(0, 1, 0));
        mat4 proj = glm::perspective(glm::radians(60.0f), float(engine.COMPUTE_WIDTH) / engine.COMPUTE_HEIGHT, 1e9f, 1e14f);
        mat4 viewProj = proj * view;
        engine.drawGrid(viewProj);

        // ---------- RUN RAYTRACER ------------- //
        glViewport(0, 0, engine.WIDTH, engine.HEIGHT);
        engine.dispatchCompute(camera);
        engine.drawFullScreenQuad();

        // 6) present to screen
        glfwSwapBuffers(engine.window);
        glfwPollEvents();
    }

    glfwDestroyWindow(engine.window);
    glfwTerminate();
    return 0;
}
