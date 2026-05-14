#include "HospitalManager.h"

#include "edge.h"
#include "graphmanager.h"
#include "graphnode.h"

#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

using namespace HospitalCapability;

HospitalManager::HospitalManager()
{
}

void HospitalManager::clear()
{
    m_hospitals.clear();
    m_indexByHospitalId.clear();
}

void HospitalManager::initializeDefaultHospitals(const QVector<int>& hospitalNodeIds)
{
    clear();

    // Simulation dataset: Egypt-based names with fictional live resources.
    // The graph node IDs come from the map generator, so the same resource
    // system works for 2x2, 2x3, 3x3, 4x4, etc.
    struct TemplateHospital {
        const char* name;
        const char* city;
        int er;
        int icu;
        int incubators;
        int ventilators;
        int wait;
        int caps;
    };

    const TemplateHospital templates[] = {
        {
            "Kasr Al Ainy Emergency Hospital", "Cairo",
            6, 3, 1, 4, 18,
            CT | MRI | OT | TRAUMA | BURN_UNIT | CATH_LAB | NEUROLOGIST | SURGEON
        },
        {
            "Ain Shams University Hospital", "Cairo",
            2, 2, 0, 2, 35,
            CT | OT | TRAUMA | CATH_LAB | SURGEON
        },
        {
            "Sheikh Zayed Specialized Hospital", "Giza",
            4, 1, 2, 3, 12,
            CT | MRI | CATH_LAB | NEUROLOGIST
        },
        {
            "Alexandria Main University Hospital", "Alexandria",
            5, 2, 1, 2, 22,
            CT | MRI | OT | TRAUMA | SURGEON
        },
        {
            "Mansoura Emergency Hospital", "Mansoura",
            3, 2, 1, 3, 16,
            CT | OT | TRAUMA | BURN_UNIT | SURGEON
        }
    };
    const int templateCount = int(sizeof(templates) / sizeof(templates[0]));

    for (int i = 0; i < hospitalNodeIds.size(); ++i) {
        const TemplateHospital& t = templates[i % templateCount];

        Hospital h;
        h.id = 1001 + i;
        h.name = QString::fromUtf8(t.name);
        h.city = QString::fromUtf8(t.city);
        h.graphNodeId = hospitalNodeIds[i];
        h.erBeds = t.er;
        h.icuBeds = t.icu;
        h.incubators = t.incubators;
        h.ventilators = t.ventilators;
        h.waitMinutes = t.wait;
        h.capabilities = t.caps;

        m_indexByHospitalId[h.id] = m_hospitals.size();
        m_hospitals.push_back(h);
    }
}

const std::vector<Hospital>& HospitalManager::hospitals() const
{
    return m_hospitals;
}

const Hospital* HospitalManager::hospitalById(int hospitalId) const
{
    auto it = m_indexByHospitalId.find(hospitalId);
    if (it == m_indexByHospitalId.end()) return nullptr;
    return &m_hospitals[it->second];
}

Hospital* HospitalManager::hospitalById(int hospitalId)
{
    auto it = m_indexByHospitalId.find(hospitalId);
    if (it == m_indexByHospitalId.end()) return nullptr;
    return &m_hospitals[it->second];
}

QString HospitalManager::statusText() const
{
    QString out;
    QTextStream s(&out);
    for (const Hospital& h : m_hospitals) {
        s << h.name << " | Node " << h.graphNodeId
          << " | ER " << h.erBeds
          << " | ICU " << h.icuBeds
          << " | Inc " << h.incubators
          << " | Vent " << h.ventilators
          << " | Wait " << h.waitMinutes << " min\n";
    }
    return out.trimmed();
}

QString HospitalManager::statusHtml() const
{
    QString html;
    html += "<table cellspacing='3' cellpadding='3'>";
    html += "<tr><th align='left'>Hospital</th><th>Node</th><th>ER</th><th>ICU</th><th>Inc</th><th>Vent</th><th>Wait</th></tr>";
    for (const Hospital& h : m_hospitals) {
        html += QString("<tr><td>%1</td><td align='center'>%2</td><td align='center'>%3</td>"
                        "<td align='center'>%4</td><td align='center'>%5</td>"
                        "<td align='center'>%6</td><td align='center'>%7 min</td></tr>")
                    .arg(h.name)
                    .arg(h.graphNodeId)
                    .arg(h.erBeds)
                    .arg(h.icuBeds)
                    .arg(h.incubators)
                    .arg(h.ventilators)
                    .arg(h.waitMinutes);
    }
    html += "</table>";
    return html;
}

QStringList HospitalManager::emergencyTypeNames() const
{
    return {
        "General emergency",
        "Severe trauma / car crash",
        "Heart attack",
        "Premature baby",
        "Head injury",
        "Burn injury",
        "Respiratory failure"
    };
}

EmergencyType HospitalManager::emergencyTypeFromIndex(int index) const
{
    switch (index) {
    case 1: return EmergencyType::SevereTrauma;
    case 2: return EmergencyType::HeartAttack;
    case 3: return EmergencyType::PrematureBaby;
    case 4: return EmergencyType::HeadInjury;
    case 5: return EmergencyType::BurnInjury;
    case 6: return EmergencyType::RespiratoryFailure;
    case 0:
    default: return EmergencyType::GeneralEmergency;
    }
}

QString HospitalManager::emergencyTypeName(EmergencyType type) const
{
    return emergencyTypeNames().value(static_cast<int>(type), "General emergency");
}

int HospitalManager::requiredCapabilities(EmergencyType type) const
{
    switch (type) {
    case EmergencyType::SevereTrauma:
        return CT | OT | TRAUMA | SURGEON;
    case EmergencyType::HeartAttack:
        return CATH_LAB;
    case EmergencyType::HeadInjury:
        return CT | NEUROLOGIST;
    case EmergencyType::BurnInjury:
        return BURN_UNIT;
    case EmergencyType::PrematureBaby:
    case EmergencyType::RespiratoryFailure:
    case EmergencyType::GeneralEmergency:
    default:
        return 0;
    }
}

bool HospitalManager::isHospitalValidForEmergency(const Hospital& h,
                                                  EmergencyType type,
                                                  QString* rejectionReason) const
{
    auto reject = [&](const QString& why) {
        if (rejectionReason) *rejectionReason = why;
        return false;
    };

    if (h.erBeds <= 0)
        return reject("ER is full");

    const int required = requiredCapabilities(type);
    if (!h.hasCapability(required))
        return reject("missing required specialist/unit/equipment");

    switch (type) {
    case EmergencyType::SevereTrauma:
        if (h.icuBeds <= 0) return reject("no ICU bed for trauma backup");
        break;
    case EmergencyType::HeartAttack:
        if (h.ventilators <= 0) return reject("no ventilator backup");
        break;
    case EmergencyType::PrematureBaby:
        if (h.incubators <= 0) return reject("no incubator available");
        break;
    case EmergencyType::RespiratoryFailure:
        if (h.icuBeds <= 0) return reject("no ICU bed available");
        if (h.ventilators <= 0) return reject("no ventilator available");
        break;
    case EmergencyType::GeneralEmergency:
    case EmergencyType::HeadInjury:
    case EmergencyType::BurnInjury:
    default:
        break;
    }

    if (rejectionReason) *rejectionReason = "valid";
    return true;
}

int HospitalManager::overloadPenalty(const Hospital& h, EmergencyType type) const
{
    int penalty = 0;

    // Load-balancing: avoid sending every ambulance to nearly-full hospitals
    // unless their route/wait score is still clearly best.
    if (h.erBeds == 1) penalty += 12;
    if (h.icuBeds == 0 && (type == EmergencyType::SevereTrauma ||
                           type == EmergencyType::RespiratoryFailure)) {
        penalty += 25;
    }
    if (h.waitMinutes > 30) penalty += 10;

    return penalty;
}

HospitalManager::DijkstraResult HospitalManager::runDijkstraOnce(GraphManager* graph,
                                                                 int startNodeId) const
{
    DijkstraResult result;
    if (!graph || !graph->getNodeByID(startNodeId)) return result;

    using Entry = std::pair<double, int>; // distance, node id
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq;

    result.dist[startNodeId] = 0.0;
    pq.push({0.0, startNodeId});

    while (!pq.empty()) {
        Entry cur = pq.top();
        pq.pop();

        const double d = cur.first;
        const int uId = cur.second;

        auto dit = result.dist.find(uId);
        if (dit == result.dist.end() || d > dit->second) continue;

        GraphNode* u = graph->getNodeByID(uId);
        if (!u) continue;

        for (Edge* e : u->getOutgoingEdges()) {
            if (!e || !e->getEndNode()) continue;
            GraphNode* v = e->getEndNode();
            const int vId = v->getID();
            const double alt = d + e->getWeight();

            auto vit = result.dist.find(vId);
            if (vit == result.dist.end() || alt < vit->second) {
                result.dist[vId] = alt;
                result.prev[vId] = uId;
                pq.push({alt, vId});
            }
        }
    }

    return result;
}

std::vector<GraphNode*> HospitalManager::reconstructPath(GraphManager* graph,
                                                         int startNodeId,
                                                         int endNodeId,
                                                         const DijkstraResult& result) const
{
    std::vector<GraphNode*> path;
    if (!graph) return path;
    if (!result.dist.count(endNodeId)) return path;

    int cur = endNodeId;
    while (true) {
        GraphNode* node = graph->getNodeByID(cur);
        if (!node) {
            path.clear();
            return path;
        }
        path.push_back(node);
        if (cur == startNodeId) break;

        auto pit = result.prev.find(cur);
        if (pit == result.prev.end()) {
            path.clear();
            return path;
        }
        cur = pit->second;
    }

    std::reverse(path.begin(), path.end());
    return path;
}

HospitalDecision HospitalManager::chooseBestHospital(GraphManager* graph,
                                                     int ambulanceStartNodeId,
                                                     EmergencyType emergencyType) const
{
    HospitalDecision decision;

    if (!graph) {
        decision.reason = "Road graph is not ready.";
        return decision;
    }
    if (m_hospitals.empty()) {
        decision.reason = "No hospitals were generated for this map.";
        return decision;
    }
    if (!graph->getNodeByID(ambulanceStartNodeId)) {
        decision.reason = "Invalid ambulance spawn node.";
        return decision;
    }

    const DijkstraResult dijkstra = runDijkstraOnce(graph, ambulanceStartNodeId);

    double bestScore = std::numeric_limits<double>::infinity();
    const Hospital* bestHospital = nullptr;
    double bestDrive = 0.0;

    QString rejectionSummary;
    QTextStream rejected(&rejectionSummary);

    for (const Hospital& h : m_hospitals) {
        QString why;
        if (!isHospitalValidForEmergency(h, emergencyType, &why)) {
            rejected << "• " << h.name << ": " << why << "\n";
            continue;
        }

        auto distIt = dijkstra.dist.find(h.graphNodeId);
        if (distIt == dijkstra.dist.end()) {
            rejected << "• " << h.name << ": unreachable from selected spawn node\n";
            continue;
        }

        const double drive = distIt->second;
        const double score = drive + h.waitMinutes + overloadPenalty(h, emergencyType);

        if (!bestHospital || score < bestScore) {
            bestScore = score;
            bestHospital = &h;
            bestDrive = drive;
        }
    }

    if (!bestHospital) {
        decision.reason = "No valid hospital is currently ready for this emergency.\n" + rejectionSummary.trimmed();
        return decision;
    }

    decision.found = true;
    decision.hospitalId = bestHospital->id;
    decision.hospitalNodeId = bestHospital->graphNodeId;
    decision.hospitalName = bestHospital->name;
    decision.driveCost = bestDrive;
    decision.waitMinutes = bestHospital->waitMinutes;
    decision.totalScore = bestScore;
    decision.path = reconstructPath(graph, ambulanceStartNodeId, bestHospital->graphNodeId, dijkstra);
    decision.reason = QString("Best hospital: %1\nDrive cost: %2\nWait time: %3 min\nTotal score: %4")
                          .arg(decision.hospitalName)
                          .arg(std::round(decision.driveCost))
                          .arg(decision.waitMinutes)
                          .arg(std::round(decision.totalScore));

    return decision;
}

bool HospitalManager::consumeResources(int hospitalId, EmergencyType emergencyType)
{
    Hospital* h = hospitalById(hospitalId);
    if (!h) return false;

    if (h->erBeds > 0) h->erBeds--;

    switch (emergencyType) {
    case EmergencyType::SevereTrauma:
        if (h->icuBeds > 0) h->icuBeds--;
        break;
    case EmergencyType::PrematureBaby:
        if (h->incubators > 0) h->incubators--;
        break;
    case EmergencyType::RespiratoryFailure:
        if (h->icuBeds > 0) h->icuBeds--;
        if (h->ventilators > 0) h->ventilators--;
        break;
    case EmergencyType::HeartAttack:
        if (h->ventilators > 0) h->ventilators--;
        break;
    case EmergencyType::GeneralEmergency:
    case EmergencyType::HeadInjury:
    case EmergencyType::BurnInjury:
    default:
        break;
    }

    h->waitMinutes += 4;
    return true;
}
