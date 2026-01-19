#include "Core/Application.h"
#include "Render/Shader.h"
#include "Render/Camera.h"
#include "Render/BoatRenderer.h"
#include "Render/TerrainRenderer.h"
#include "Render/ObjectRenderer.h"
#include "Water/WaterSurface.h"
#include "Editor/SceneEditor.h"
#include "Editor/EditorUI.h"
#include "Physics/Boat.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <iostream>
#include <vector>
#include <cmath>

using namespace WaterTown;

/**
 * @brief 场景编辑器应用
 */
class WaterTownApp : public Application {
public:
    WaterTownApp() : Application(1280, 720, "WaterTown - Scene Editor") {}
    
protected:
    void onInit() override {
        std::cout << "Initializing WaterTown App..." << std::endl;
        
        // 启用深度测试
        glEnable(GL_DEPTH_TEST);
        
        // 创建立方体顶点数据（位置 + 法线）
        createCubeData();
        
        // 加载着色器
        m_shader = new Shader("assets/shaders/basic.vert", "assets/shaders/basic.frag");
        m_waterShader = new Shader("assets/shaders/water.vert", "assets/shaders/water.frag");
        m_skyShader = new Shader("assets/shaders/sky.vert", "assets/shaders/sky.frag");
        m_cloudShader = new Shader("assets/shaders/clouds.vert", "assets/shaders/clouds.frag");
        
        // 创建水面 - 适应扩展的网格 (X:160, Z:1600)
        // 降低分辨率从 100 到 40 以提升性能
        m_waterSurface = new WaterSurface(0.0f, 0.0f, 160.0f, 1600.0f, 40);
        m_waterSurface->setBaseHeight(SceneEditor::WATER_LEVEL);  // 水面高度
        
        // 创建场景编辑器
        m_sceneEditor = new SceneEditor();
        float aspectRatio = getWindow()->getAspectRatio();
        m_sceneEditor->init(aspectRatio);
        m_sceneEditor->setWaterSurface(m_waterSurface);
        
        // 创建船只渲染器
        m_boatRenderer = new BoatRenderer();
        
        // 创建地形渲染器
        m_terrainRenderer = new TerrainRenderer(SceneEditor::GRID_SIZE_X, SceneEditor::INITIAL_GRID_SIZE_Z);
        
        // 创建物体渲染器
        m_objectRenderer = new ObjectRenderer();

        // 创建云朵网格与实例
        createCloudQuad();
        initClouds();
        
        // 创建编辑器 UI
        m_editorUI = new EditorUI();
        m_editorUI->init(m_sceneEditor);
        
        // 使用编辑器的相机（默认从地形编辑模式开始）
        m_camera = m_sceneEditor->getCurrentCamera();
        
        // 设置窗口回调
        auto* window = getWindow()->getGLFWWindow();
        glfwSetWindowUserPointer(window, this);
        
        // 窗口大小改变回调
        auto resizeCallback = [](GLFWwindow* win, int width, int height) {
            glViewport(0, 0, width, height);
            auto* app = static_cast<WaterTownApp*>(glfwGetWindowUserPointer(win));
            if (app && app->m_sceneEditor) {
                float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
                app->m_sceneEditor->updateAspectRatio(aspectRatio);
            }
        };
        getWindow()->setResizeCallback(resizeCallback);
        
        // 不手动设置鼠标回调，让 ImGui 处理
        // 我们将在 onUpdate 中直接获取鼠标位置
        
        // 默认不启用鼠标捕获，按住鼠标右键时才启用
        m_mouseCaptured = false;
        getWindow()->setCursorCapture(false);
        
        std::cout << "WaterTown App initialized successfully!" << std::endl;
        std::cout << "Controls:" << std::endl;
        std::cout << "  WASD - Move camera" << std::endl;
        std::cout << "  Space/Shift - Up/Down" << std::endl;
        std::cout << "  Hold Right Mouse Button - Look around" << std::endl;
        std::cout << "  ESC - Exit" << std::endl;
    }
    
    void onUpdate(float deltaTime) override {
        auto* window = getWindow()->getGLFWWindow();
        
        // 处理键盘输入
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);
        
        // 检测鼠标左键(用于地形/建筑编辑)
        bool wantCaptureMouse = ImGui::GetIO().WantCaptureMouse;
        static bool leftButtonPressed = false;
        int leftButtonState = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
        
        // 检查是否按住 Ctrl 键
        bool ctrlPressed = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) ||
                          (glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
        
        // Ctrl+Z 撤销快捷键
        static bool zKeyPressed = false;
        if (ctrlPressed && glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS && !zKeyPressed) {
            zKeyPressed = true;
            if (m_sceneEditor) {
                m_sceneEditor->undoLastAction();
            }
        }
        else if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_RELEASE) {
            zKeyPressed = false;
        }
        
        // 地形编辑模式:支持按住鼠标左键连续绘制
        if (m_sceneEditor && m_sceneEditor->getCurrentMode() == EditorMode::TERRAIN) {
            if (leftButtonState == GLFW_PRESS && !wantCaptureMouse) {
                double xpos, ypos;
                glfwGetCursorPos(window, &xpos, &ypos);
                int width, height;
                glfwGetWindowSize(window, &width, &height);
                m_sceneEditor->handleMouseClick(static_cast<float>(xpos), static_cast<float>(ypos), width, height);
            }
        }
        // 建筑放置模式:单击放置
        else if (leftButtonState == GLFW_PRESS && !leftButtonPressed && !wantCaptureMouse) {
            leftButtonPressed = true;
            
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);
            int width, height;
            glfwGetWindowSize(window, &width, &height);
            
            if (m_sceneEditor) {
                if (ctrlPressed && m_sceneEditor->getCurrentMode() == EditorMode::BUILDING) {
                    // Ctrl + 左键:删除建筑物
                    int gridX, gridZ;
                    if (m_sceneEditor->raycastToGround(static_cast<float>(xpos), static_cast<float>(ypos), width, height, gridX, gridZ)) {
                        float cellSize = 0.5f;
                        float worldX = (gridX - 25 + 0.5f) * cellSize;
                        float worldZ = (gridZ - 25 + 0.5f) * cellSize;
                        m_sceneEditor->removeObjectNear(glm::vec3(worldX, 0.0f, worldZ), 1.0f);
                    }
                } else {
                    // 普通左键:放置建筑
                    m_sceneEditor->handleMouseClick(static_cast<float>(xpos), static_cast<float>(ypos), width, height);
                }
            }
        }
        
        if (leftButtonState == GLFW_RELEASE) {
            leftButtonPressed = false;
        }
        
        // 检测鼠标右键状态，但只在鼠标不在 ImGui 窗口上时捕获
        int rightButtonState = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);
        bool shouldCapture = (rightButtonState == GLFW_PRESS) && !wantCaptureMouse;
        
        // 只有在状态改变时才更新鼠标捕获模式
        if (shouldCapture != m_mouseCaptured) {
            m_mouseCaptured = shouldCapture;
            getWindow()->setCursorCapture(m_mouseCaptured);
            
            // 重置鼠标位置，避免跳动
            if (m_mouseCaptured) {
                m_firstMouse = true;
            }
        }
        
        // 处理鼠标移动（当鼠标被捕获时）
        if (m_mouseCaptured && m_sceneEditor) {
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);
            
            if (m_firstMouse) {
                m_lastX = static_cast<float>(xpos);
                m_lastY = static_cast<float>(ypos);
                m_firstMouse = false;
            }
            
            float xoffset = static_cast<float>(xpos) - m_lastX;
            float yoffset = m_lastY - static_cast<float>(ypos);
            
            m_lastX = static_cast<float>(xpos);
            m_lastY = static_cast<float>(ypos);
            
            m_sceneEditor->handleMouseMovement(xoffset, yoffset, true);
        }
        
        // 检测中键拖动(用于建筑模式平移相机)
        static bool middleButtonPressed = false;
        static bool middleFirstMouse = true;
        static float middleLastX = 0.0f, middleLastY = 0.0f;
        int middleButtonState = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE);
        
        if (middleButtonState == GLFW_PRESS && !wantCaptureMouse && m_sceneEditor) {
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);
            
            if (!middleButtonPressed || middleFirstMouse) {
                middleLastX = static_cast<float>(xpos);
                middleLastY = static_cast<float>(ypos);
                middleFirstMouse = false;
                middleButtonPressed = true;
            } else {
                float xoffset = static_cast<float>(xpos) - middleLastX;
                float yoffset = static_cast<float>(ypos) - middleLastY;
                
                middleLastX = static_cast<float>(xpos);
                middleLastY = static_cast<float>(ypos);
                
                m_sceneEditor->handleMiddleMouseMovement(xoffset, yoffset);
            }
        }
        else if (middleButtonState == GLFW_RELEASE) {
            middleButtonPressed = false;
            middleFirstMouse = true;
        }
        
        // 游戏模式下处理船只控制 (WASD)
        if (m_sceneEditor && m_sceneEditor->getCurrentMode() == EditorMode::GAME) {
            float forward = 0.0f;
            float turn = 0.0f;
            
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) forward += 1.0f;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) forward -= 1.0f;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) turn += 1.0f;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) turn -= 1.0f;
            
            m_sceneEditor->handleGameInput(forward, turn);
        }
        
        // 更新场景编辑器
        if (m_sceneEditor) {
            m_sceneEditor->update(deltaTime);
            m_camera = m_sceneEditor->getCurrentCamera();  // 更新当前相机
        }

        // 更新船尾波浪效果
        if (m_sceneEditor->getBoat()) {
            auto boat = m_sceneEditor->getBoat();
            m_waterSurface->updateWake(
                deltaTime,
                boat->getPosition(),
                glm::vec2(sin(glm::radians(boat->getRotation())), 
                          cos(glm::radians(boat->getRotation()))),
                boat->getSpeed()  // 获取速度大小
            );
        }

        updateClouds(deltaTime);
    }
    
    void onRender() override {
        if (!m_shader || !m_camera) return;

        // === 渲染天空盒 ===
        if (m_skyShader && m_cubeVAO) {
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);

            m_skyShader->use();
            glm::mat4 view = glm::mat4(glm::mat3(m_camera->getViewMatrix()));
            m_skyShader->setMat4("uView", view);
            m_skyShader->setMat4("uProjection", m_camera->getProjectionMatrix());

            glBindVertexArray(m_cubeVAO);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            glBindVertexArray(0);

            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
        }

        // === 渲染云朵 ===
        if (m_cloudShader && m_cloudVAO) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            glDisable(GL_DEPTH_TEST);

            m_cloudShader->use();
            m_cloudShader->setMat4("uView", m_camera->getViewMatrix());
            m_cloudShader->setMat4("uProjection", m_camera->getProjectionMatrix());

            glm::vec3 camPos = m_camera->getPosition();
            for (const auto& cloud : m_clouds) {
                glm::vec3 worldPos(camPos.x + cloud.offsetXZ.x, cloud.height, camPos.z + cloud.offsetXZ.y);
                glm::vec3 toCam = glm::normalize(glm::vec3(camPos.x - worldPos.x, 0.0f, camPos.z - worldPos.z));
                float yaw = std::atan2(toCam.x, toCam.z);

                glm::mat4 model(1.0f);
                model = glm::translate(model, worldPos);
                model = glm::rotate(model, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
                model = glm::scale(model, glm::vec3(cloud.size, cloud.size * 0.6f, 1.0f));
                m_cloudShader->setMat4("uModel", model);
                m_cloudShader->setFloat("uAlpha", cloud.alpha);

                glBindVertexArray(m_cloudVAO);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glBindVertexArray(0);
            }

            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
        }
        
        // === 旋转立方体已注释 ===
        // m_shader->use();
        // glm::mat4 model = glm::mat4(1.0f);
        // model = glm::translate(model, glm::vec3(0.0f, 0.5f, 0.0f));
        // model = glm::rotate(model, (float)glfwGetTime() * glm::radians(50.0f), glm::vec3(0.5f, 1.0f, 0.0f));
        // glm::mat4 view = m_camera->getViewMatrix();
        // glm::mat4 projection = m_camera->getProjectionMatrix();
        // m_shader->setMat4("uModel", model);
        // m_shader->setMat4("uView", view);
        // m_shader->setMat4("uProjection", projection);
        // glm::vec3 lightPos(1.2f, 1.0f, 2.0f);
        // m_shader->setVec3("uLightPos", lightPos);
        // m_shader->setVec3("uViewPos", m_camera->getPosition());
        // m_shader->setVec3("uObjectColor", 1.0f, 0.5f, 0.31f);
        // m_shader->setVec3("uLightColor", 1.0f, 1.0f, 1.0f);
        // glBindVertexArray(m_cubeVAO);
        // glDrawArrays(GL_TRIANGLES, 0, 36);
        // glBindVertexArray(0);
        
        // === 渲染地形网格(所有模式) ===
        if (m_sceneEditor && m_terrainRenderer && m_shader) {
            m_terrainRenderer->render(m_sceneEditor, m_shader, m_camera);
        }
        
        // === 渲染放置的物体(所有模式) ===
        if (m_sceneEditor && m_objectRenderer && m_shader) {
            m_objectRenderer->clear();
            const auto& objects = m_sceneEditor->getPlacedObjects();
            for (const auto& obj : objects) {
                m_objectRenderer->addObject(obj.first, obj.second);
            }
            m_objectRenderer->render(m_shader, m_camera);
        }
        
        // === 渲染水面(仅在非地形编辑模式) ===
        if (m_waterSurface && m_waterShader && m_sceneEditor) {
            if (m_sceneEditor->getCurrentMode() != EditorMode::TERRAIN) {
                glm::vec3 boatPos(0.0f);
                glm::vec2 boatForwardXZ(0.0f, 1.0f);
                glm::vec2 boatHalfExtentsXZ(0.0f);
                float boatFeather = 0.0f;
                float boatSpeed = 0.0f;

                EditorMode mode = m_sceneEditor->getCurrentMode();
                
                // 获取船速
                if (mode == EditorMode::GAME && m_sceneEditor->getBoat()) {
                    boatSpeed = m_sceneEditor->getBoat()->getSpeed();
                }
                
                // 裁剪区域大小与速度成正比，使用四次方让低速时区域极小
                float speedFactor = glm::clamp(boatSpeed / 15.0f, 0.0f, 1.0f);
                speedFactor = speedFactor * speedFactor * speedFactor * speedFactor;  // 四次方
                
                if (m_boatRenderer && boatSpeed > 4.0f) {  // 超过4 m/s显示
                    // 超过6 m/s后，从0.01开始映射到1.0
                    float scaledFactor = 0.01f + speedFactor * 0.99f;  // 0.01 -> 1.0
                    boatHalfExtentsXZ = m_boatRenderer->getWaterCutoutHalfExtentsXZ(0.35f * scaledFactor);
                    boatFeather = 0.3f + scaledFactor * 0.5f;
                } else {
                    // 速度低于6 m/s时完全禁用
                    boatHalfExtentsXZ = glm::vec2(0.0f);
                    boatFeather = 0.0f;
                }

                if (mode == EditorMode::GAME && m_sceneEditor->getBoat()) {
                    boatPos = m_sceneEditor->getBoat()->getPosition();
                    float rot = m_sceneEditor->getBoat()->getRotation();
                    float rotRad = glm::radians(rot);
                    boatForwardXZ = glm::vec2(std::sin(rotRad), std::cos(rotRad));
                } else if (mode == EditorMode::BUILDING && m_sceneEditor->hasBoatPlaced()) {
                    boatPos = m_sceneEditor->getBoatPlacedPosition();
                    float rot = m_sceneEditor->getBoatPlacedRotation();
                    float rotRad = glm::radians(rot);
                    boatForwardXZ = glm::vec2(std::sin(rotRad), std::cos(rotRad));
                    // 放置模式下使用固定大小
                    boatHalfExtentsXZ = m_boatRenderer->getWaterCutoutHalfExtentsXZ(0.35f);
                    boatFeather = 0.8f;
                } else {
                    boatHalfExtentsXZ = glm::vec2(0.0f);
                    boatFeather = 0.0f;
                }

                m_waterSurface->render(
                    m_waterShader,
                    m_camera,
                    static_cast<float>(glfwGetTime()),
                    boatPos,
                    0.0f,
                    0.0f,
                    boatForwardXZ,
                    boatHalfExtentsXZ,
                    boatFeather
                );
            }
        }
        
        // === 渲染船只(建筑模式和游戏模式) ===
        if (m_sceneEditor && m_boatRenderer && m_shader) {
            EditorMode mode = m_sceneEditor->getCurrentMode();
            if (mode == EditorMode::GAME) {
                // 游戏模式:渲染可控船只
                if (m_sceneEditor->getBoat()) {
                    m_boatRenderer->render(m_sceneEditor->getBoat(), m_shader, m_camera);
                }
            }
            else if (mode == EditorMode::BUILDING && m_sceneEditor->hasBoatPlaced()) {
                // 建筑模式:渲染已放置的船只(静态显示)
                // 使用放置位置创建临时Boat渲染
                Boat tempBoat(m_sceneEditor->getBoatPlacedPosition(), m_sceneEditor->getBoatPlacedRotation());
                m_boatRenderer->render(&tempBoat, m_shader, m_camera);
            }
        }
    }
    
    void onImGui() override {
        // 使用编辑器 UI
        if (m_editorUI) {
            m_editorUI->render();
        }
    }
    
    void onShutdown() override {
        std::cout << "Shut down" << std::endl;
        
        // 清理资源
        if (m_cubeVAO) {
            glDeleteVertexArrays(1, &m_cubeVAO);
            glDeleteBuffers(1, &m_cubeVBO);
        }
        if (m_cloudVAO) {
            glDeleteVertexArrays(1, &m_cloudVAO);
            glDeleteBuffers(1, &m_cloudVBO);
        }
        
        delete m_shader;
        delete m_waterShader;
        delete m_skyShader;
        delete m_cloudShader;
        delete m_waterSurface;
        delete m_sceneEditor;
        delete m_editorUI;
        delete m_boatRenderer;
        delete m_terrainRenderer;
        delete m_objectRenderer;
        // 注意：m_camera 由 SceneEditor 管理，不需要单独删除
        
        std::cout << "WaterTown Demo shutdown complete." << std::endl;
    }

private:
    Shader* m_shader = nullptr;
    Shader* m_waterShader = nullptr;
    Shader* m_skyShader = nullptr;
    Shader* m_cloudShader = nullptr;
    WaterSurface* m_waterSurface = nullptr;
    SceneEditor* m_sceneEditor = nullptr;
    EditorUI* m_editorUI = nullptr;
    BoatRenderer* m_boatRenderer = nullptr;
    TerrainRenderer* m_terrainRenderer = nullptr;
    ObjectRenderer* m_objectRenderer = nullptr;
    Camera* m_camera = nullptr;  // 指向当前相机（由 SceneEditor 管理）
    
    unsigned int m_cubeVAO = 0;
    unsigned int m_cubeVBO = 0;

    unsigned int m_cloudVAO = 0;
    unsigned int m_cloudVBO = 0;

    struct CloudInstance {
        glm::vec2 offsetXZ;
        float height;
        float size;
        float alpha;
        glm::vec2 velocityXZ;
    };
    std::vector<CloudInstance> m_clouds;
    
    bool m_firstMouse = true;
    float m_lastX = 640.0f;
    float m_lastY = 360.0f;
    bool m_mouseCaptured = false;
    
    void createCubeData() {
        // 立方体顶点数据（位置 + 法线）
        float vertices[] = {
            // 后面
            -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
             0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
             0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
             0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
            -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
            -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
            
            // 前面
            -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
             0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
             0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
             0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
            -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
            -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
            
            // 左面
            -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
            
            // 右面
             0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
             0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
             0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
             0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
             0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
             0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
            
            // 底面
            -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
             0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
             0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
             0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
            -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
            -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
            
            // 顶面
            -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
             0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
             0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
             0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
            -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
            -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f
        };
        
        // 创建 VAO 和 VBO
        glGenVertexArrays(1, &m_cubeVAO);
        glGenBuffers(1, &m_cubeVBO);
        
        glBindVertexArray(m_cubeVAO);
        
        glBindBuffer(GL_ARRAY_BUFFER, m_cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        
        // 位置属性
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        
        // 法线属性
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        
        glBindVertexArray(0);
        
        std::cout << "Cube VAO/VBO created successfully." << std::endl;
    }

    void createCloudQuad() {
        float quad[] = {
            // positions        // uv
            -0.5f, 0.0f, 0.0f,  0.0f, 0.0f,
             0.5f, 0.0f, 0.0f,  1.0f, 0.0f,
             0.5f, 1.0f, 0.0f,  1.0f, 1.0f,

            -0.5f, 0.0f, 0.0f,  0.0f, 0.0f,
             0.5f, 1.0f, 0.0f,  1.0f, 1.0f,
            -0.5f, 1.0f, 0.0f,  0.0f, 1.0f
        };

        glGenVertexArrays(1, &m_cloudVAO);
        glGenBuffers(1, &m_cloudVBO);

        glBindVertexArray(m_cloudVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_cloudVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);
    }

    void initClouds() {
        m_clouds.clear();
        m_clouds.push_back({glm::vec2(-60.0f, -80.0f), 55.0f, 28.0f, 0.75f, glm::vec2(0.8f, 0.25f)});
        m_clouds.push_back({glm::vec2(50.0f, -90.0f), 60.0f, 30.0f, 0.7f, glm::vec2(0.6f, 0.2f)});
        m_clouds.push_back({glm::vec2(-90.0f, -30.0f), 65.0f, 24.0f, 0.65f, glm::vec2(0.7f, 0.3f)});
        m_clouds.push_back({glm::vec2(70.0f, -40.0f), 58.0f, 26.0f, 0.7f, glm::vec2(0.65f, 0.22f)});
        m_clouds.push_back({glm::vec2(-30.0f, 60.0f), 62.0f, 32.0f, 0.7f, glm::vec2(0.55f, 0.28f)});
        m_clouds.push_back({glm::vec2(60.0f, 80.0f), 56.0f, 26.0f, 0.65f, glm::vec2(0.75f, 0.18f)});
        m_clouds.push_back({glm::vec2(-75.0f, 90.0f), 68.0f, 34.0f, 0.7f, glm::vec2(0.62f, 0.26f)});
        m_clouds.push_back({glm::vec2(20.0f, 95.0f), 64.0f, 28.0f, 0.65f, glm::vec2(0.58f, 0.2f)});
    }

    void updateClouds(float deltaTime) {
        const float bounds = 120.0f;
        for (auto& cloud : m_clouds) {
            cloud.offsetXZ += cloud.velocityXZ * deltaTime;

            if (cloud.offsetXZ.x > bounds) cloud.offsetXZ.x = -bounds;
            if (cloud.offsetXZ.x < -bounds) cloud.offsetXZ.x = bounds;
            if (cloud.offsetXZ.y > bounds) cloud.offsetXZ.y = -bounds;
            if (cloud.offsetXZ.y < -bounds) cloud.offsetXZ.y = bounds;
        }
    }
};

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "WaterTown - Basic Rendering System" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        WaterTownApp app;
        app.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return -1;
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "Program exited successfully." << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}
