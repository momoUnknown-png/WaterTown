#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <deque>

namespace WaterTown {

// 船只尾流粒子系统
class BoatWake {
public:
    struct WakeParticle {
        glm::vec3 position;
        float amplitude;       // 波的振幅
        float wavelength;      // 波长
        float lifetime;        // 粒子剩余寿命（秒）
        float age;             // 粒子年龄（秒）
        glm::vec2 direction;   // 传播方向
        float speed;           // 传播速度
    };

    BoatWake();
    ~BoatWake();

    // 更新尾流系统
    void update(float deltaTime, const glm::vec3& boatPos, const glm::vec2& boatForward, float boatSpeed);

    // 添加新的尾流粒子（在船头或船尾位置）
    void emitWake(const glm::vec3& position, const glm::vec2& direction, float intensity);

    // 获取所有活跃粒子
    const std::vector<WakeParticle>& getParticles() const { return m_particles; }
    
    // 获取当前船速（用于动态调整wake影响范围）
    float getCurrentBoatSpeed() const { return m_currentBoatSpeed; }

    // 清空所有粒子
    void clear();

    // 参数设置
    void setMaxParticles(int count) { m_maxParticles = count; }
    void setEmissionRate(float rate) { m_emissionRate = rate; }
    void setParticleLifetime(float lifetime) { m_particleLifetime = lifetime; }

private:
    std::vector<WakeParticle> m_particles;
    int m_maxParticles;
    float m_emissionRate;      // 每秒发射粒子数
    float m_particleLifetime;  // 粒子存活时间
    float m_accumulator;       // 用于控制发射频率

    // 船头波参数
    float m_bowWaveAmplitude;
    float m_bowWaveWavelength;

    // 船尾波参数  
    float m_sternWaveAmplitude;
    float m_sternWaveWavelength;
    
    // 当前船速（用于动态调整）
    float m_currentBoatSpeed;
};

} // namespace WaterTown
