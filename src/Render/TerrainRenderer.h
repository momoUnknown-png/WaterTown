#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include "../Editor/SceneEditor.h"

namespace WaterTown {

class Shader;
class Camera;

/**
 * @brief 地形网格渲染器
 */
class TerrainRenderer {
public:
    TerrainRenderer(int gridSizeX, int gridSizeZ);
    ~TerrainRenderer();
    
    /**
     * @brief 渲染地形网格
     * @param editor 场景编辑器（获取地形数据）
     * @param shader 着色器
     * @param camera 相机
     */
    void render(SceneEditor* editor, Shader* shader, Camera* camera);
    
    /**
     * @brief 设置网格大小
     */
    void setGridSize(int sizeX, int sizeZ) { 
        m_gridSizeX = sizeX; 
        m_gridSizeZ = sizeZ;
        m_terrainDirty = true;  // 标记需要重建
    }
    
    /**
     * @brief 标记地形需要更新
     */
    void markDirty() { m_terrainDirty = true; }
    
private:
    int m_gridSizeX;
    int m_gridSizeZ;
    
    struct TerrainVertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec3 color;
    };
    
    GLuint m_planeVAO, m_planeVBO;
    
    // 缓存相关
    bool m_terrainDirty = true;  // 标记地形是否需要重建
    std::vector<TerrainVertex> m_cachedVertices;  // 缓存的顶点数据
    glm::vec3 m_lastCameraPos = glm::vec3(0.0f);  // 上次渲染时的相机位置
    
    // 增加 addWallBricks 声明，这在 Sec 版本的 cpp 中用到，但在 h 文件中通常是辅助函数，这里显式声明以便使用
    // 注意：如果在 cpp 中是类成员函数，则需要在此声明；如果是静态辅助函数则不需要。
    // 根据先前的 view_file 结果，addWallBricks 似乎是一个私有成员函数。
    void addWallBricks(std::vector<TerrainVertex>& vertices, float x, float z, float size, 
                      bool top, bool bottom, bool left, bool right);
                      
    void buildTerrainVertices(SceneEditor* editor, std::vector<TerrainVertex>& outVertices, const glm::vec3& cameraPos);
    glm::vec3 getTerrainColor(TerrainType type) const;
    float getTerrainHeight(TerrainType type) const;
};

} // namespace WaterTown
