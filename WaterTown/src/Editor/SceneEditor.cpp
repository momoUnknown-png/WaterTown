#include "Core/Application.h"
#include "Editor/SceneEditor.h"
#include <GLFW/glfw3.h>
#include "Render/OrthographicCamera.h"
#include "Render/OrbitCamera.h"
#include "Render/FollowCamera.h"
#include "Render/Camera.h"
#include "Render/ObjectRenderer.h"
#include "Water/WaterSurface.h"
#include "Physics/Boat.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <set>

namespace WaterTown {

SceneEditor::SceneEditor()
    : m_currentMode(EditorMode::TERRAIN),
      m_orthoCamera(nullptr),
      m_orbitCamera(nullptr),
      m_followCamera(nullptr),
      m_waterSurface(nullptr),
      m_boat(nullptr),
      m_objectRenderer(nullptr),
      m_riverStartColumn(0),
      m_riverEndColumn(0),
      m_currentTerrainType(TerrainType::GRASS),
      m_currentObjectType(ObjectType::HOUSE),
      m_objectsHiddenForGame(false),
      m_boatPlaced(false),
      m_boatPlacedPosition(0.0f),
      m_currentGridZ(INITIAL_GRID_SIZE_Z) {
      
    // 初始化动态网格为320x320，默认全空
    m_terrainGrid.resize(GRID_SIZE_X, std::vector<TerrainType>(INITIAL_GRID_SIZE_Z, TerrainType::EMPTY));
}

SceneEditor::~SceneEditor() {
    delete m_orthoCamera;
    delete m_orbitCamera;
    delete m_followCamera;
    delete m_boat;
    delete m_objectRenderer;
}

// --- Transition Camera Helper ---
class TransitionCamera : public Camera {
public:
    glm::mat4 viewMatrix;
    glm::vec3 position;
    float aspect = 1.77f;
    
    TransitionCamera() : viewMatrix(glm::mat4(1.0f)), position(glm::vec3(0.0f)) {}
    
    glm::mat4 getViewMatrix() const override { return viewMatrix; }
    glm::mat4 getProjectionMatrix() const override { 
        return glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f); 
    }
    void setPosition(const glm::vec3& p) override { position = p; }
    glm::vec3 getPosition() const override { return position; }
    
    void setLookAt(const glm::vec3& eye, const glm::vec3& center, const glm::vec3& up) {
        viewMatrix = glm::lookAt(eye, center, up);
        position = eye;
    }
    void setAspectRatio(float a) { aspect = a; }
};

void SceneEditor::init(float aspectRatio) {
    // 创建三种相机
    m_orthoCamera = new OrthographicCamera(0.0f, 0.0f, 160.0f, 160.0f);
    m_orthoCamera->setHeight(140.0f);

    // 建筑模式：更广 FOV、更远距离与更深远裁剪，营造真实空间感
    m_orbitCamera = new OrbitCamera(glm::vec3(0.0f, 0.0f, 0.0f), 48.0f, 60.0f, aspectRatio);
    m_orbitCamera->setDistanceLimits(8.0f, 120.0f);
    m_orbitCamera->setClipPlanes(0.1f, 3200.0f);  // 适应扩展的Z尺寸
    m_orbitCamera->setAngles(225.0f, 38.0f);
    
    m_followCamera = new FollowCamera(45.0f, aspectRatio);
    
    // 过渡相机
    m_transitionCamera = new TransitionCamera();
    static_cast<TransitionCamera*>(m_transitionCamera)->setAspectRatio(aspectRatio);
    
    // 创建船只 (暂不初始化位置，等待放置或生成)
    m_boat = new Boat(glm::vec3(0.0f, -100.0f, 0.0f), 0.0f); // 初始藏起来
    float halfExtentX = GRID_SIZE_X * CELL_SIZE * 0.5f;
    float halfExtentZ = INITIAL_GRID_SIZE_Z * CELL_SIZE * 0.5f;  // 初始Z尺寸
    m_boat->setBounds(-halfExtentX, halfExtentX, -halfExtentZ, halfExtentZ);
    
    // 设置地形碰撞回调
    m_boat->setCollisionPredicate([this](float x, float z) {
        float cell = CELL_SIZE;
        int gx = static_cast<int>(std::floor(x / cell + GRID_SIZE_X / 2.0f));
        int gz = static_cast<int>(std::floor(z / cell + m_currentGridZ / 2.0f));
        
        // 越界视为碰撞
        if (gx < 0 || gx >= GRID_SIZE_X || gz < 0 || gz >= m_currentGridZ) return false;
        
        // 只有 WATER 视为安全
        return m_terrainGrid[gx][gz] == TerrainType::WATER;
    });

    // 创建物体渲染器
    m_objectRenderer = new ObjectRenderer();

    // 初始化默认地形（江南水乡）
    initializeTerrainLayout();
    
    // 确保水面网格更新
    updateWaterMesh();
    
    // 清理可能存在的水上物体
    removeObjectsOnWaterExceptBoat();
    
    std::cout << "SceneEditor initialized with Jiangnan Water Town layout." << std::endl;
}

void SceneEditor::initializeTerrainLayout() {
    // 江南地形生成逻辑 (移植自 WaterTown-sec)
    
    // 1. 先全部设为草地 (WaterTown 默认为 EMPTY，这里为了场景效果先填满草地，或者保留 EMPTY 作为边缘？)
    // 根据需求，我们希望有一个默认场景。WaterTown-sec 是填满的。
    // 我们保留边缘为 EMPTY，中间为场景？
    // 为了匹配 Sec 的效果，我们将整个 GRID 填满。
    
    for (int x = 0; x < GRID_SIZE_X; ++x) {
        for (int z = 0; z < INITIAL_GRID_SIZE_Z; ++z) {
            m_terrainGrid[x][z] = TerrainType::WATER; // 先铺满水，类似威尼斯/江南
        }
    }
    
    // 定义河道宽度和岸线厚度
    const int riverWidth = static_cast<int>(GRID_SIZE_X * 0.25f);   // 中央河道宽 25%
    const int bankWidth = 3;                                    // 石质河岸厚度
    const int center = GRID_SIZE_X / 2;
    m_riverStartColumn = center - riverWidth / 2;
    m_riverEndColumn = m_riverStartColumn + riverWidth;
    
    // 生成陆地
    for (int x = 0; x < GRID_SIZE_X; ++x) {
        TerrainType columnType;
        if (x >= m_riverStartColumn && x < m_riverEndColumn) {
            columnType = TerrainType::WATER; // 河道
        }
        else if ((x >= m_riverStartColumn - bankWidth && x < m_riverStartColumn) ||
                 (x >= m_riverEndColumn && x < m_riverEndColumn + bankWidth)) {
            columnType = TerrainType::STONE; // 石砌河岸
        }
        else {
            columnType = TerrainType::GRASS; // 城镇陆地
        }
        
        for (int z = 0; z < INITIAL_GRID_SIZE_Z; ++z) {
            m_terrainGrid[x][z] = columnType;
        }
    }
    
    // 在石岸上加几段码头/广场
    int plazaDepth = INITIAL_GRID_SIZE_Z / 5;
    for (int z = INITIAL_GRID_SIZE_Z / 3; z < INITIAL_GRID_SIZE_Z / 3 + plazaDepth; ++z) {
        for (int x = m_riverStartColumn - bankWidth - 3; x < m_riverStartColumn - bankWidth; ++x) {
            if (x >= 0) m_terrainGrid[x][z] = TerrainType::STONE;
        }
        for (int x = m_riverEndColumn + bankWidth; x < m_riverEndColumn + bankWidth + 3; ++x) {
            if (x < GRID_SIZE_X) m_terrainGrid[x][z] = TerrainType::STONE;
        }
    }
    
    // 扩展Z方向10倍，复制模式
    int newZ = INITIAL_GRID_SIZE_Z * 10;
    for (int x = 0; x < GRID_SIZE_X; ++x) {
        m_terrainGrid[x].resize(newZ);
        for (int z = INITIAL_GRID_SIZE_Z; z < newZ; ++z) {
            m_terrainGrid[x][z] = m_terrainGrid[x][z % INITIAL_GRID_SIZE_Z];  // 复制地形模式
        }
    }
    m_currentGridZ = newZ;

    // 按需求：保留船前方全部，删除船后方 4/5
    trimBackSection();
    
    // 标记船只放置状态（默认场景没有船，或者我们可以放一艘？）
    // Sec 在 init 里 placeBoat。我们可以放一艘在河中心。
    float riverCenterX = (m_riverStartColumn + m_riverEndColumn) * 0.5f * CELL_SIZE - (GRID_SIZE_X / 2.0f * CELL_SIZE) + CELL_SIZE * 0.5f;
    float centerZ = 0.0f;
    
    // 初始放置船只在河中心
    glm::vec3 initialBoatPos(riverCenterX, 0.2f, centerZ);
    float initialBoatRot = 0.0f; // 船头右转90度
    
    m_boat->setPosition(initialBoatPos);
    m_boat->setRotation(initialBoatRot);
    m_boatPlaced = true;
    m_boatPlacedPosition = initialBoatPos;
    m_boatPlacedRotation = initialBoatRot;
    // 更新船只边界以适应扩展的网格
    float halfExtentX = GRID_SIZE_X * CELL_SIZE * 0.5f;
    float halfExtentZ = m_currentGridZ * CELL_SIZE * 0.5f;
    m_boat->setBounds(-halfExtentX, halfExtentX, -halfExtentZ, halfExtentZ);
    
    // 清空历史记录
    m_terrainHistory.clear();
    m_objectHistory.clear();
}

void SceneEditor::update(float deltaTime) {
    // 更新过渡状态
    if (m_isTransitioning) {
        m_transitionTime += deltaTime;
        float t = glm::clamp(m_transitionTime / m_transitionDuration, 0.0f, 1.0f);
        
        // Easing: Smoothstep
        float smoothT = t * t * (3.0f - 2.0f * t);
        
        glm::vec3 currentPos = glm::mix(m_transStartPos, m_transEndPos, smoothT);
        glm::vec3 currentTarget = glm::mix(m_transStartTarget, m_transEndTarget, smoothT);
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        
        if (m_transitionCamera) {
            static_cast<TransitionCamera*>(m_transitionCamera)->setLookAt(currentPos, currentTarget, up);
        }
        
        if (t >= 1.0f) {
            m_isTransitioning = false;
        }
    }

    float currentTime = static_cast<float>(glfwGetTime());

    // 只在游戏模式下更新船只物理（运动、碰撞）
    // 在其他模式下只更新浮力效果（视觉上的水波浮动）
    if (m_currentMode == EditorMode::GAME) {
        if (m_boat && m_waterSurface) {
            m_boat->update(deltaTime, m_waterSurface, currentTime);
        }
    } else {
        // 非游戏模式：只同步水面高度，不更新运动
        if (m_boat && m_waterSurface) {
            m_boat->syncToWaterSurface(m_waterSurface, currentTime);
        }
    }

    // 游戏模式下更新追随相机
    if (m_currentMode == EditorMode::GAME && m_followCamera && m_boat) {
        m_followCamera->setTarget(m_boat->getPosition(), m_boat->getRotation());
        m_followCamera->update(deltaTime);
    }
}

void SceneEditor::setWaterSurface(WaterSurface* water) {
    m_waterSurface = water;
    updateWaterMesh(); // 初始设置时生成网格
    if (m_boat && m_waterSurface) {
        m_boat->syncToWaterSurface(m_waterSurface, static_cast<float>(glfwGetTime()));
    }
}

void SceneEditor::updateWaterMesh() {
    if (!m_waterSurface) return;

    // 收集所有 WATER 类型的格子，生成网格数据传给 WaterSurface
    std::vector<float> vertices; 
    
    float halfSizeX = GRID_SIZE_X / 2.0f;
    float halfSizeZ = m_currentGridZ / 2.0f;
    float uvScale = 0.1f; // UV 缩放因子
    
    for (int x = 0; x < GRID_SIZE_X; ++x) {
        for (int z = 0; z < m_currentGridZ; ++z) {
            if (m_terrainGrid[x][z] == TerrainType::WATER) {
                float x0 = (x - halfSizeX) * CELL_SIZE;
                float z0 = (z - halfSizeZ) * CELL_SIZE;
                float x1 = x0 + CELL_SIZE;
                float z1 = z0 + CELL_SIZE;
                float y = 0.0f; // Base level
                
                // Triangle 1
                // Vertex 0 (x0, z0)
                vertices.push_back(x0); vertices.push_back(y); vertices.push_back(z0);
                vertices.push_back(x0 * uvScale); vertices.push_back(z0 * uvScale);
                
                // Vertex 1 (x0, z1)
                vertices.push_back(x0); vertices.push_back(y); vertices.push_back(z1);
                vertices.push_back(x0 * uvScale); vertices.push_back(z1 * uvScale);
                
                // Vertex 2 (x1, z0)
                vertices.push_back(x1); vertices.push_back(y); vertices.push_back(z0);
                vertices.push_back(x1 * uvScale); vertices.push_back(z0 * uvScale);
                
                // Triangle 2
                // Vertex 3 (x1, z0)
                vertices.push_back(x1); vertices.push_back(y); vertices.push_back(z0);
                vertices.push_back(x1 * uvScale); vertices.push_back(z0 * uvScale);
                
                // Vertex 4 (x0, z1)
                vertices.push_back(x0); vertices.push_back(y); vertices.push_back(z1);
                vertices.push_back(x0 * uvScale); vertices.push_back(z1 * uvScale);
                
                // Vertex 5 (x1, z1)
                vertices.push_back(x1); vertices.push_back(y); vertices.push_back(z1);
                vertices.push_back(x1 * uvScale); vertices.push_back(z1 * uvScale);
            }
        }
    }
    
    m_waterSurface->updateMesh(vertices);
}


void SceneEditor::switchMode(EditorMode mode) {
    if (m_currentMode == mode) return;
    
    EditorMode oldMode = m_currentMode;
    
    // === 离开游戏模式时：保存船只当前状态 ===
    if (oldMode == EditorMode::GAME && m_boat && m_boatPlaced) {
        m_boatPlacedPosition = m_boat->getPosition();
        m_boatPlacedRotation = m_boat->getRotation();
    }
    
    // === 进入游戏模式时：恢复船只状态 ===
    if (mode == EditorMode::GAME && m_boat && m_boatPlaced) {
        m_boat->setPosition(m_boatPlacedPosition);
        m_boat->setRotation(m_boatPlacedRotation);
        m_boat->setSpeed(0.0f);
    }
    
    // === 相机过渡动画 ===
    Camera* oldCam = getCurrentCamera();
    glm::vec3 startPos = oldCam->getPosition();
    glm::vec3 startTarget = (oldMode == EditorMode::BUILDING && m_orbitCamera) 
        ? m_orbitCamera->getTarget() 
        : (m_boat ? m_boat->getPosition() : glm::vec3(0.0f));
    
    // 更新到新模式
    m_currentMode = mode;
    
    // === 配置新模式的相机 ===
    if (mode == EditorMode::TERRAIN) {
        m_orthoCamera->setCenter(0.0f, 0.0f);
        m_isTransitioning = false;
    }
    else if (mode == EditorMode::BUILDING) {
        if (m_boatPlaced) {
            m_orbitCamera->setTarget(m_boatPlacedPosition);
        }
        m_transEndTarget = m_orbitCamera->getTarget();
        m_transEndPos = m_orbitCamera->getPosition();
        
        // Game -> Building 启用过渡
        if (oldMode == EditorMode::GAME) {
            m_isTransitioning = true;
            m_transitionTime = 0.0f;
            m_transStartPos = startPos;
            m_transStartTarget = startTarget;
        } else {
            m_isTransitioning = false;
        }
    }
    else if (mode == EditorMode::GAME) {
        snapObjectsToTerrain();
        if (m_boat && m_followCamera) {
            m_followCamera->setTarget(m_boat->getPosition(), m_boat->getRotation());
            // 使用 getDesiredPosition 计算考虑船朝向后的正确相机位置
            m_followCamera->setPosition(m_followCamera->getDesiredPosition());
        }
        
        if (m_boat) {
            m_boat->clearObstacles();
            updateBoatObstacles();
            
            m_transEndTarget = m_boat->getPosition();
            m_transEndPos = m_followCamera->getDesiredPosition();
        }
        
        // Building -> Game 启用过渡
        if (oldMode == EditorMode::BUILDING) {
            m_isTransitioning = true;
            m_transitionTime = 0.0f;
            m_transStartPos = startPos;
            m_transStartTarget = startTarget;
        } else {
            m_isTransitioning = false;
        }
    }
    
    removeObjectsOnWaterExceptBoat();
}

void SceneEditor::updateAspectRatio(float aspectRatio) {
    if (m_orbitCamera) m_orbitCamera->updateAspectRatio(aspectRatio);
    if (m_followCamera) m_followCamera->updateAspectRatio(aspectRatio);
}

void SceneEditor::removeObjectsOnWaterExceptBoat() {
    auto isWaterCell = [&](const glm::vec3& p) {
        float cell = CELL_SIZE;
        int gx = static_cast<int>(std::floor(p.x / cell + GRID_SIZE_X / 2.0f));
        int gz = static_cast<int>(std::floor(p.z / cell + m_currentGridZ / 2.0f));
        if (gx < 0 || gx >= GRID_SIZE_X || gz < 0 || gz >= m_currentGridZ) return false;
        return m_terrainGrid[gx][gz] == TerrainType::WATER;
    };

    auto prune = [&](std::vector<std::pair<ObjectType, glm::vec3>>& list) {
        auto it = std::remove_if(list.begin(), list.end(), [&](const auto& obj) {
            // 允许特定物体在水上
            if (obj.first == ObjectType::BOAT) return false;
            // 桥、水榭、码头、荷花池、渔船 可以在水上
            if (obj.first == ObjectType::BRIDGE || 
                obj.first == ObjectType::ARCH_BRIDGE ||
                obj.first == ObjectType::WATER_PAVILION ||
                obj.first == ObjectType::PIER ||
                obj.first == ObjectType::LOTUS_POND ||
                obj.first == ObjectType::FISHING_BOAT) return false;
                
            return isWaterCell(obj.second);
        });
        if (it != list.end()) {
            list.erase(it, list.end());
        }
    };

    prune(m_placedObjects);
    // prune(m_hiddenObjects); 
}

void SceneEditor::placeTerrain(int gridX, int gridZ, TerrainType type) {
    if (gridX < 0 || gridX >= GRID_SIZE_X || gridZ < 0 || gridZ >= m_currentGridZ) return;
    
    TerrainType oldType = m_terrainGrid[gridX][gridZ];
    if (oldType == type) return; // 无变化
    
    // 记录撤销
    m_terrainHistory.push_back({gridX, gridZ, oldType, type});
    
    m_terrainGrid[gridX][gridZ] = type;
    
    // 如果涉及水面变化，更新网格
    if (oldType == TerrainType::WATER || type == TerrainType::WATER) {
        updateWaterMesh();
    }
    
    std::cout << "Placed terrain " << static_cast<int>(type) << " at (" << gridX << "," << gridZ << ")" << std::endl;
}

void SceneEditor::placeObject(ObjectType type, const glm::vec3& position) {
    // 仅允许陆地建筑类型
    const bool isAllowed = (type == ObjectType::HOUSE ||
                            type == ObjectType::HOUSE_STYLE_1 ||
                            type == ObjectType::HOUSE_STYLE_2 ||
                            type == ObjectType::HOUSE_STYLE_3 ||
                            type == ObjectType::HOUSE_STYLE_4 ||
                            type == ObjectType::HOUSE_STYLE_5 ||
                            type == ObjectType::WALL ||
                            type == ObjectType::TREE ||
                            type == ObjectType::BAMBOO ||
                            type == ObjectType::PLANT_1 ||
                            type == ObjectType::PLANT_2 ||
                            type == ObjectType::PLANT_4 ||
                            type == ObjectType::TEMPLE);
    if (!isAllowed) {
        std::cout << "Object type is disabled by building rules." << std::endl;
        return;
    }

    // 检查是否允许放置
    int gridX, gridZ;
    // 反算 grid
    float cell = CELL_SIZE;
    gridX = static_cast<int>(std::floor(position.x / cell + GRID_SIZE_X / 2.0f));
    gridZ = static_cast<int>(std::floor(position.z / cell + m_currentGridZ / 2.0f));
    
    if (gridX >= 0 && gridX < GRID_SIZE_X && gridZ >= 0 && gridZ < m_currentGridZ) {
        TerrainType tType = m_terrainGrid[gridX][gridZ];
        bool isWater = (tType == TerrainType::WATER);
        
        if (isWater) {
            std::cout << "Cannot place this object on water!" << std::endl;
            return;
        }
    }

    // 记录撤销
    m_objectHistory.push_back({type, position, true}); // isAdd = true
    
    glm::vec3 adjustedPos = position;
    adjustedPos.y = getTerrainHeightAt(position.x, position.z);
    m_placedObjects.push_back({type, adjustedPos});
    
    if (type == ObjectType::BOAT) {
        m_boat->setPosition(position);
        // 保留当前旋转（如果已有值），或者新放置的船默认使用 m_boat 当前的旋转
        m_boatPlaced = true;
        m_boatPlacedPosition = position;
        m_boatPlacedRotation = m_boat->getRotation(); // 同步当前旋转值
    }
    
    // 游戏模式下实时更新
    if (m_currentMode == EditorMode::GAME) {
        updateBoatObstacles();
    }
    
    std::cout << "Placed object " << static_cast<int>(type) << " at " << position.x << "," << position.z << std::endl;
}

void SceneEditor::undoLastAction() {
    // 优先撤销物体（如果最近操作是物体？）
    // 简单的双栈撤销逻辑比较复杂。
    // 这里我们简单起见：检查两个 history 哪个非空。
    // 为了更精确，通常需要一个全局操作栈。
    // 但鉴于接口限制，我们先检查 Object 栈，再检查 Terrain 栈 (假设用户倾向于撤销最近的物体)
    // 或者我们给 history 加时间戳？
    // WaterTown 原版可能只是简单的分开撤销。
    // 让我们假设：按模式撤销。
    
    if (m_currentMode == EditorMode::BUILDING && !m_objectHistory.empty()) {
        ObjectAction action = m_objectHistory.back();
        m_objectHistory.pop_back();
        
        if (action.isAdd) {
            // 撤销添加 -> 删除
            // 寻找并删除该物体 (从后往前找)
            for (auto it = m_placedObjects.rbegin(); it != m_placedObjects.rend(); ++it) {
                if (it->first == action.type && glm::length(it->second - action.position) < 0.01f) {
                    // 找到，删除（转换为正向迭代器）
                    m_placedObjects.erase(std::next(it).base());
                    break;
                }
            }
        } else {
            // 撤销删除 -> 添加
            m_placedObjects.push_back({action.type, action.position});
        }
        std::cout << "Undid object action." << std::endl;
    }
    else if (m_currentMode == EditorMode::TERRAIN && !m_terrainHistory.empty()) {
        TerrainAction action = m_terrainHistory.back();
        m_terrainHistory.pop_back();
        
        m_terrainGrid[action.gridX][action.gridZ] = action.oldType;
        
        if (action.oldType == TerrainType::WATER || action.newType == TerrainType::WATER) {
            updateWaterMesh();
        }
        std::cout << "Undid terrain action." << std::endl;
    }
}

void SceneEditor::handleMiddleMouseMovement(float deltaX, float deltaY) {
    // 中键平移，仅在 BUILDING 模式有效（模拟 RTS/CAD）
    if (m_currentMode == EditorMode::BUILDING && m_orbitCamera) {
        // 反转输入方向以实现"抓取拖动"效果
        // 修正：将 deltaY 映射到 Z 轴 (前后)，deltaX 映射到 X 轴 (左右)
        // 用户反馈：左右移动方向反了。
        // 原来: panTarget(-deltaY, 0, -deltaX) -> 导致左右反向
        // 现在: panTarget(-deltaY, 0, deltaX) -> 翻转 X 轴符号
        m_orbitCamera->panTarget(-deltaY * 0.02f, 0.0f, deltaX * 0.02f);
    }
}

void SceneEditor::handleMouseMovement(float deltaX, float deltaY, bool rightButtonPressed) {
    if (!rightButtonPressed) return;
    
    switch (m_currentMode) {
        case EditorMode::TERRAIN:
            if (m_orthoCamera) m_orthoCamera->pan(deltaX * 0.05f, deltaY * 0.05f);
            break;
        case EditorMode::BUILDING:
            if (m_orbitCamera) m_orbitCamera->rotate(deltaX, deltaY);
            break;
        case EditorMode::GAME:
            // 游戏模式由右键旋转相机
            if (m_followCamera) {
                m_followCamera->rotate(deltaX, deltaY);
            }
            break;
    }
}

void SceneEditor::handleMouseScroll(float delta) {
    if (m_currentMode == EditorMode::TERRAIN && m_orthoCamera) {
        m_orthoCamera->zoom(delta);
    } else if (m_currentMode == EditorMode::BUILDING && m_orbitCamera) {
        m_orbitCamera->zoom(delta * 0.5f);
    }
}

Camera* SceneEditor::getCurrentCamera() {
    if (m_isTransitioning) {
        return m_transitionCamera; 
    }
    switch (m_currentMode) {
        case EditorMode::TERRAIN: return m_orthoCamera;
        case EditorMode::BUILDING: return m_orbitCamera;
        case EditorMode::GAME: return m_followCamera;
        default: return m_orbitCamera;
    }
}

TerrainType SceneEditor::getTerrainAt(int gridX, int gridZ) const {
    if (gridX < 0 || gridX >= GRID_SIZE_X || gridZ < 0 || gridZ >= m_currentGridZ) return TerrainType::EMPTY;
    return m_terrainGrid[gridX][gridZ];
}

bool SceneEditor::isWaterAt(int gridX, int gridZ) const {
    return getTerrainAt(gridX, gridZ) == TerrainType::WATER;
}

float SceneEditor::getRiverCenterWorldX() const {
    float centerColumn = (m_riverStartColumn + m_riverEndColumn) * 0.5f;
    return (centerColumn - GRID_SIZE_X / 2.0f) * CELL_SIZE;
}

float SceneEditor::getTerrainHeightForType(TerrainType type) const {
    switch (type) {
        case TerrainType::GRASS:
            return 1.0f;
        case TerrainType::STONE:
            return 1.1f;
        case TerrainType::WATER:
            return WATER_LEVEL;
        case TerrainType::EMPTY:
        default:
            return 0.0f;
    }
}

float SceneEditor::getTerrainHeightAt(float worldX, float worldZ) const {
    float cell = CELL_SIZE;
    int gridX = static_cast<int>(std::floor(worldX / cell + GRID_SIZE_X / 2.0f));
    int gridZ = static_cast<int>(std::floor(worldZ / cell + m_currentGridZ / 2.0f));
    if (gridX < 0 || gridX >= GRID_SIZE_X || gridZ < 0 || gridZ >= m_currentGridZ) {
        return 0.0f;
    }
    return getTerrainHeightForType(getTerrainAt(gridX, gridZ));
}

void SceneEditor::snapObjectsToTerrain() {
    for (auto& obj : m_placedObjects) {
        obj.second.y = getTerrainHeightAt(obj.second.x, obj.second.z);
    }
}

void SceneEditor::trimBackSection() {
    const float totalLengthZ = m_currentGridZ * CELL_SIZE;
    const float keepMinZ = -0.1f * totalLengthZ; // 后半段保留 1/5

    // 裁剪地形：将后方 4/5 置为空
    for (int z = 0; z < m_currentGridZ; ++z) {
        float worldZ = (z - m_currentGridZ / 2.0f) * CELL_SIZE + CELL_SIZE * 0.5f;
        if (worldZ < keepMinZ) {
            for (int x = 0; x < GRID_SIZE_X; ++x) {
                m_terrainGrid[x][z] = TerrainType::EMPTY;
            }
        }
    }

    // 移除被裁剪区域的建筑
    auto it = std::remove_if(m_placedObjects.begin(), m_placedObjects.end(),
        [keepMinZ](const std::pair<ObjectType, glm::vec3>& obj) {
            return obj.second.z < keepMinZ;
        });
    m_placedObjects.erase(it, m_placedObjects.end());

    // 更新水面
    updateWaterMesh();
}

void SceneEditor::handleGameInput(float forward, float turn) {
    if (m_currentMode == EditorMode::GAME && m_boat) {
        m_boat->processInput(forward, turn);
    }
}

bool SceneEditor::raycastToGround(float screenX, float screenY, int screenWidth, int screenHeight, int& outGridX, int& outGridZ) {
    Camera* camera = getCurrentCamera();
    if (!camera) return false;
    
    // NDC
    float x = (2.0f * screenX) / screenWidth - 1.0f;
    float y = 1.0f - (2.0f * screenY) / screenHeight;
    
    glm::mat4 projection = camera->getProjectionMatrix();
    glm::mat4 view = camera->getViewMatrix();
    glm::mat4 invVP = glm::inverse(projection * view);
    
    glm::vec4 rayStart = invVP * glm::vec4(x, y, -1.0f, 1.0f);
    glm::vec4 rayEnd = invVP * glm::vec4(x, y, 1.0f, 1.0f);
    rayStart /= rayStart.w;
    rayEnd /= rayEnd.w;
    
    glm::vec3 dir = glm::normalize(glm::vec3(rayEnd) - glm::vec3(rayStart));
    
    // Plane intersect Y=0 (or WATER_LEVEL if mode is boat?)
    // Usually terrain is around Y=0.
    float groundY = 0.0f; // Simplified
    if (std::abs(dir.y) < 0.001f) return false;
    
    float t = (groundY - rayStart.y) / dir.y;
    if (t < 0) return false;
    
    glm::vec3 hit = glm::vec3(rayStart) + dir * t;
    
    float cell = CELL_SIZE;
    outGridX = static_cast<int>(std::floor(hit.x / cell + GRID_SIZE_X / 2.0f));
    outGridZ = static_cast<int>(std::floor(hit.z / cell + m_currentGridZ / 2.0f));
    
    return (outGridX >= 0 && outGridX < GRID_SIZE_X && outGridZ >= 0 && outGridZ < m_currentGridZ);
}

void SceneEditor::handleMouseClick(float screenX, float screenY, int screenWidth, int screenHeight) {
    int gx, gz;
    if (raycastToGround(screenX, screenY, screenWidth, screenHeight, gx, gz)) {
        if (m_currentMode == EditorMode::TERRAIN) {
            placeTerrain(gx, gz, m_currentTerrainType);
        } else if (m_currentMode == EditorMode::BUILDING) {
            float cell = CELL_SIZE;
            float wx = (gx - GRID_SIZE_X / 2.0f) * cell + cell * 0.5f; // Center of tile
            float wz = (gz - m_currentGridZ / 2.0f) * cell + cell * 0.5f;
            float wy = getTerrainHeightForType(getTerrainAt(gx, gz));
            placeObject(m_currentObjectType, glm::vec3(wx, wy, wz));
        }
    }
}

void SceneEditor::updateBoatObstacles() {
    if (!m_boat) return;
    m_boat->clearObstacles();
    for (const auto& obj : getPlacedObjects()) {
        float r = 1.0f;
        if (obj.first == ObjectType::HOUSE) r = 1.5f;
        m_boat->addObstacle(obj.second, r);
    }
}

void SceneEditor::removeLastObject() {
    if (!m_placedObjects.empty()) {
        m_placedObjects.pop_back();
        if (m_currentMode == EditorMode::GAME) updateBoatObstacles();
    }
}

bool SceneEditor::removeObjectNear(const glm::vec3& worldPos, float radius) {
    auto& objs = m_placedObjects;
    for (auto it = objs.begin(); it != objs.end(); ++it) {
        if (glm::distance(glm::vec3(it->second.x, 0, it->second.z), glm::vec3(worldPos.x, 0, worldPos.z)) < radius) {
            // 记录撤销删除
            m_objectHistory.push_back({it->first, it->second, false}); // isAdd = false
            
            objs.erase(it);
            if (m_currentMode == EditorMode::GAME) updateBoatObstacles();
            return true;
        }
    }
    return false;
}

void SceneEditor::clearAllObjects() {
    m_placedObjects.clear();
    m_objectHistory.clear(); 
    if (m_currentMode == EditorMode::GAME) updateBoatObstacles();
}

void SceneEditor::clearScene() {
    clearAllObjects();
    // 恢复默认地形
    initializeTerrainLayout();
    updateWaterMesh();
    std::cout << "Scene reset." << std::endl;
}

bool SceneEditor::saveScene(const std::string& filename) {
    // 简化实现
    std::ofstream out(filename);
    if (!out) return false;
    out << GRID_SIZE_X << " " << m_currentGridZ << "\n";
    for(int i=0; i<GRID_SIZE_X; ++i) {
        for(int j=0; j<m_currentGridZ; ++j) {
            out << (int)m_terrainGrid[i][j] << " ";
        }
        out << "\n";
    }
    out << m_placedObjects.size() << "\n";
    for(const auto& obj : m_placedObjects) {
        out << (int)obj.first << " " << obj.second.x << " " << obj.second.y << " " << obj.second.z << "\n";
    }
    return true;
}

bool SceneEditor::loadScene(const std::string& filename) {
    std::ifstream in(filename);
    if (!in) return false;
    int sizeX, sizeZ;
    in >> sizeX >> sizeZ;
    if (sizeX != GRID_SIZE_X || sizeZ > m_currentGridZ) return false;  // 只加载兼容的
    for(int i=0; i<sizeX; ++i) {
        for(int j=0; j<sizeZ; ++j) {
            int t; in >> t; m_terrainGrid[i][j] = (TerrainType)t;
        }
    }
    int count;
    in >> count;
    m_placedObjects.clear();
    for(int i=0; i<count; ++i) {
        int t; float x,y,z;
        in >> t >> x >> y >> z;
        m_placedObjects.push_back({(ObjectType)t, glm::vec3(x,y,z)});
    }
    trimBackSection();
    snapObjectsToTerrain();
    updateWaterMesh();
    return true;
}

// __PLACEHOLDER_PART_3__



} // namespace WaterTown
