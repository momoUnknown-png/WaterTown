#include "BoatWake.h"
#include <algorithm>

namespace WaterTown {

BoatWake::BoatWake()
    : m_maxParticles(20)
    , m_emissionRate(5.0f)  // 每秒5个粒子
    , m_particleLifetime(5.0f)  // 5秒寿命
    , m_accumulator(0.0f)
    , m_bowWaveAmplitude(0.3f)
    , m_bowWaveWavelength(2.0f)
    , m_sternWaveAmplitude(0.2f)
    , m_sternWaveWavelength(3.0f)
    , m_currentBoatSpeed(0.0f)
{
    m_particles.reserve(m_maxParticles);
}

BoatWake::~BoatWake() {
    clear();
}

void BoatWake::update(float deltaTime, const glm::vec3& boatPos, const glm::vec2& boatForward, float boatSpeed) {
    // 存储当前船速
    m_currentBoatSpeed = boatSpeed;
    
    // 更新现有粒子
    for (auto it = m_particles.begin(); it != m_particles.end(); ) {
        it->age += deltaTime;
        
        // 粒子沿方向传播
        it->position.x += it->direction.x * it->speed * deltaTime;
        it->position.z += it->direction.y * it->speed * deltaTime;
        
        // 振幅随时间衰减
        float lifeRatio = it->age / it->lifetime;
        it->amplitude *= (1.0f - lifeRatio * 0.1f * deltaTime);
        
        // 移除过期粒子
        if (it->age >= it->lifetime || it->amplitude < 0.01f) {
            it = m_particles.erase(it);
        } else {
            ++it;
        }
    }

    // 根据船速调整发射率
    float speedFactor = glm::clamp(boatSpeed / 10.0f, 0.0f, 1.0f);
    float adjustedRate = m_emissionRate * speedFactor;

    // 累积时间并发射新粒子
    m_accumulator += deltaTime;
    float interval = 1.0f / adjustedRate;

    while (m_accumulator >= interval && m_particles.size() < m_maxParticles) {
        m_accumulator -= interval;

        // 计算船头和船尾位置
        glm::vec2 boatRight(-boatForward.y, boatForward.x);  // 船的右向量
        float boatLength = 3.0f;  // 假设船长约3米
        float boatWidth = 1.5f;   // 船宽约1.5米

        // 船头位置（前方）
        glm::vec3 bowPos = boatPos + glm::vec3(boatForward.x, 0, boatForward.y) * boatLength * 0.5f;
        
        // 船尾左右两侧位置（产生V字型尾流）
        glm::vec3 sternLeftPos = boatPos - glm::vec3(boatForward.x, 0, boatForward.y) * boatLength * 0.3f
                                          + glm::vec3(boatRight.x, 0, boatRight.y) * boatWidth * 0.4f;
        glm::vec3 sternRightPos = boatPos - glm::vec3(boatForward.x, 0, boatForward.y) * boatLength * 0.3f
                                           - glm::vec3(boatRight.x, 0, boatRight.y) * boatWidth * 0.4f;

        // 船头波：向前和两侧扩散
        {
            float angle = glm::radians(30.0f);  // 船头波角度
            for (int i = -1; i <= 1; i++) {
                float spreadAngle = angle * i;
                glm::vec2 waveDir = glm::vec2(
                    boatForward.x * cos(spreadAngle) - boatForward.y * sin(spreadAngle),
                    boatForward.x * sin(spreadAngle) + boatForward.y * cos(spreadAngle)
                );
                emitWake(bowPos, glm::normalize(waveDir), speedFactor * m_bowWaveAmplitude);
            }
        }

        // 船尾左侧波浪
        {
            // 尾流与航向成约20-40度角（Kelvin wake pattern）
            float wakeAngle = glm::radians(30.0f);
            glm::vec2 wakeDir = glm::vec2(
                -boatForward.x * cos(wakeAngle) + boatRight.x * sin(wakeAngle),
                -boatForward.x * sin(wakeAngle) + boatRight.y * cos(wakeAngle)
            );
            emitWake(sternLeftPos, glm::normalize(wakeDir), speedFactor * m_sternWaveAmplitude);
        }

        // 船尾右侧波浪
        {
            float wakeAngle = glm::radians(30.0f);
            glm::vec2 wakeDir = glm::vec2(
                -boatForward.x * cos(wakeAngle) - boatRight.x * sin(wakeAngle),
                -boatForward.x * sin(wakeAngle) - boatRight.y * cos(wakeAngle)
            );
            emitWake(sternRightPos, glm::normalize(wakeDir), speedFactor * m_sternWaveAmplitude);
        }
    }
}

void BoatWake::emitWake(const glm::vec3& position, const glm::vec2& direction, float intensity) {
    if (m_particles.size() >= m_maxParticles) {
        return;
    }

    WakeParticle particle;
    particle.position = position;
    particle.direction = direction;
    particle.amplitude = intensity;
    particle.wavelength = m_sternWaveWavelength;
    particle.speed = 2.0f;  // 波传播速度
    particle.lifetime = m_particleLifetime;
    particle.age = 0.0f;

    m_particles.push_back(particle);
}

void BoatWake::clear() {
    m_particles.clear();
    m_accumulator = 0.0f;
}

} // namespace WaterTown
