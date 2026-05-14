#ifndef HOSPITALMANAGER_H
#define HOSPITALMANAGER_H

#include "Hospital.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <cstddef>
#include <unordered_map>
#include <vector>

class GraphManager;
class GraphNode;

struct HospitalDecision {
    bool found = false;

    int hospitalId = -1;
    int hospitalNodeId = -1;
    QString hospitalName;
    QString reason;

    double driveCost = 0.0;
    int waitMinutes = 0;
    double totalScore = 0.0;

    std::vector<GraphNode*> path;
};

class HospitalManager
{
public:
    HospitalManager();

    void clear();
    void initializeDefaultHospitals(const QVector<int>& hospitalNodeIds);

    const std::vector<Hospital>& hospitals() const;
    const Hospital* hospitalById(int hospitalId) const;
    Hospital* hospitalById(int hospitalId);

    QString statusText() const;
    QString statusHtml() const;

    QStringList emergencyTypeNames() const;
    EmergencyType emergencyTypeFromIndex(int index) const;
    QString emergencyTypeName(EmergencyType type) const;

    HospitalDecision chooseBestHospital(GraphManager* graph,
                                        int ambulanceStartNodeId,
                                        EmergencyType emergencyType) const;

    bool consumeResources(int hospitalId, EmergencyType emergencyType);

private:
    std::vector<Hospital> m_hospitals;
    std::unordered_map<int, std::size_t> m_indexByHospitalId;

    int requiredCapabilities(EmergencyType type) const;
    bool isHospitalValidForEmergency(const Hospital& h,
                                     EmergencyType type,
                                     QString* rejectionReason = nullptr) const;
    int overloadPenalty(const Hospital& h, EmergencyType type) const;

    struct DijkstraResult {
        std::unordered_map<int, double> dist;
        std::unordered_map<int, int> prev;
    };

    DijkstraResult runDijkstraOnce(GraphManager* graph, int startNodeId) const;
    std::vector<GraphNode*> reconstructPath(GraphManager* graph,
                                            int startNodeId,
                                            int endNodeId,
                                            const DijkstraResult& result) const;
};

#endif // HOSPITALMANAGER_H
