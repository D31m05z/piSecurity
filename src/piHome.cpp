#include "piHome.h"

#define _USE_MATH_DEFINES
#include <cmath>

static void error_callback(int error, const char* description)
{
    fprintf(stderr, "Error %d: %s\n", error, description);
}

PIHome::PIHome()
    : exiting_(false)
    , warnStep_(0)
{
    printf( "piHome initializing ...\n" );

    // initialize wiringPiSetupGpio
    if ( wiringPiSetupGpio() == -1 ) {
        throw std::runtime_error("[E] wiringPiSetupGpio failed");
    }

    // Setup window
    glfwSetErrorCallback(error_callback);
    if (!glfwInit()) {
        throw std::runtime_error("[E] glfwInit failed");
    }

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor == nullptr) {
        throw std::runtime_error("Could not get primary monitor. This is probably caused by using RDP.");
    }

    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (mode == nullptr) {
        throw std::runtime_error("Could not get video mode. This is probably caused by using RDP.");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#ifdef GLFW_CONTEXT_CREATION_API
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
#endif
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_ALPHA_BITS, 0);
    glfwWindowHint(GLFW_DECORATED, true);

    window_ = glfwCreateWindow(mode->width, mode->height, "piHome", monitor, nullptr);
    if (window_ == nullptr) {
        throw std::runtime_error("Could not create GLFW window.");
    }
    glfwMakeContextCurrent(window_);

    // Setup ImGui binding
    ImGui_ImplGlfw_Init(window_, true);

    // initialize sensors
    //sensors_.push_back(new DHT11Sensor("Raspberry Pi DHT11/DHT22 temperature/humidity", 4, 85));
    sensors_.push_back(new MQ135Sensor("Raspberry Pi MQ-135 Gas sensor", 19));
    sensors_.push_back(new PIRSensor("Raspberry Pi PIR Motion decetor sensor", 20));
    sensors_.push_back(new CameraSensor("Raspberry Pi Camera sensor"));

    printf( "SENSORS: \n" );
    for(int i=0; i < sensors_.size(); i++) {
        printf("%s\n", sensors_[i]->name().c_str());
    }

    // start sensors
    //sensor_thread_ = std::thread (&PIHome::sensor_thread_func, PIHome());
}

PIHome::~PIHome()
{
    exiting_.store(true, std::memory_order_relaxed);

    if(sensor_thread_.joinable()) {
        sensor_thread_.join();
    }

    // Cleanup
    for(int i=0; i < sensors_.size(); i++) {
        delete sensors_[i];
    }

    ImGui_ImplGlfw_Shutdown();
    glfwTerminate();
}

// update loop
void PIHome::update()
{
    for(int i = 0; i < sensors_.size(); i++) {
        sensors_[i]->update();
    }

    usleep(100000); // 10fps
    //delay( 1000 ); /* wait 1 second before next read */
}

// draw loop
void PIHome::draw()
{
    // imgui
    glfwPollEvents();
    ImGui_ImplGlfw_NewFrame();
    ImGui::Begin("piHome");
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    if(ImGui::Button("Exit")) {
        exiting_.store(true, std::memory_order_relaxed);
    }
    ImGui::End();

    for(int i=0; i < sensors_.size(); i++) {
        bool warning = sensors_[i]->getWarning();
        if (warning) {
            float red = (float)(sin((++warnStep_ * 25 * M_PI / 180.0) + 1.0) / 4.0 + 0.5);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(red, 0.0f, 0.0f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(red, 0.0f, 0.0f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(red, 0.0f, 0.0f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(red, 0.0f, 0.0f, 0.8f));
        }

        ImGui::Begin(sensors_[i]->name().c_str());

        // TODO remove dynamic cast, use template data instead
        CameraSensor* camera = dynamic_cast<CameraSensor*>(sensors_[i]);
        if(camera != nullptr) {
            ImGui::Text("Image");
            if(ImGui::Button("Take picture")) {
                camera->takePicture();
            }

            RaspiImage image = camera->getImage();
            glBindTexture(GL_TEXTURE_2D, image.textureID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.width, image.height, 0, GL_RGB, GL_UNSIGNED_BYTE, image.data.data());
            ImGui::Image((void*)(uintptr_t)image.textureID,
                         ImVec2(320, 240),
                         ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, 255), ImColor(255, 255, 255, 128));
        }

        PIRSensor* pir = dynamic_cast<PIRSensor*>(sensors_[i]);
        if(pir != nullptr) {
            PIRData data = pir->getData();
            if(data.timeinfo != nullptr) {
                ImGui::Text("Last detected local time and date: %s", asctime(data.timeinfo));
            }
            ImGui::Text("Detected counter %d", data.count);
            if(data.detected) {
                ImGui::Text("MOTION DETECTED");
            }
        }

        MQ135Sensor* mq135 = dynamic_cast<MQ135Sensor*>(sensors_[i]);
        if(mq135 != nullptr) {
            if(mq135->getDetected()) {
                ImGui::Text("GAS detected");
            } else {
                ImGui::Text("OK");
            }
        }

        ImGui::End();

        if (warning) {
            ImGui::PopStyleColor();
            ImGui::PopStyleColor();
            ImGui::PopStyleColor();
            ImGui::PopStyleColor();
        }
    }

    // Rendering
    int display_w, display_h;
    glfwGetFramebufferSize(window_, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.7f, 0.7f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui::Render();
    glfwSwapBuffers(window_);
}

bool PIHome::isRunning()
{
    return (!glfwWindowShouldClose(window_) && !exiting_.load(std::memory_order_relaxed));
}