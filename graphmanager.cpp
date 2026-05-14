#include "graphmanager.h"

#include "graphnode.h"
#include "edge.h"

#include <algorithm>
#include <limits>
#include <queue>
#include <unordered_map>
#include <utility>

GraphManager::GraphManager()
{
}

GraphManager::~GraphManager()
{
    clear();
}

void GraphManager::addNode(GraphNode* node)
{
    m_nodes.push_back(node);
}

void GraphManager::connectNodes(GraphNode* start, GraphNode* end)
{
    // Create edge
    Edge* edge = new Edge(start, end);

    // Store edge globally
    m_edges.push_back(edge);

    // Store edge locally inside node
    start->addEdge(edge);
}

void GraphManager::clear()
{
    for (Edge* edge : m_edges) {
        delete edge;
    }
    m_edges.clear();

    // Do not delete GraphNode pointers here because they are QGraphicsItems
    // owned by QGraphicsScene once scene->addItem(node) is called.
    m_nodes.clear();
}

std::vector<GraphNode*> GraphManager::getNodes() const
{
    return m_nodes;
}

std::vector<Edge*> GraphManager::getEdges() const
{
    return m_edges;
}

GraphNode* GraphManager::getNodeByID(int id) const
{
    for(GraphNode* node : m_nodes)
    {
        if(node->getID() == id)
        {
            return node;
        }
    }

    return nullptr;
}

// ── Dijkstra shortest path ──────────────────────────────────────────────
// Standard Dijkstra with a binary-heap frontier. The frontier holds
// (distance, node) pairs; we pop the smallest distance, relax its outgoing
// edges, and push any improved neighbours. Stops early once `end` is popped.
std::vector<GraphNode*> GraphManager::shortestPath(GraphNode* start,
                                                    GraphNode* end) const
{
    std::vector<GraphNode*> path;
    if (!start || !end) return path;
    if (start == end) { path.push_back(start); return path; }

    using NodeDist = std::pair<float, GraphNode*>;   // (distance, node)
    std::priority_queue<NodeDist,
                        std::vector<NodeDist>,
                        std::greater<NodeDist>> frontier;

    std::unordered_map<int, float>       dist;
    std::unordered_map<int, GraphNode*>  prev;

    dist[start->getID()] = 0.0f;
    frontier.push({ 0.0f, start });

    while (!frontier.empty()) {
        NodeDist top = frontier.top(); frontier.pop();
        float     d = top.first;
        GraphNode* u = top.second;

        if (u == end) break;
        auto itD = dist.find(u->getID());
        if (itD == dist.end() || d > itD->second) continue;  // stale entry

        for (Edge* e : u->getOutgoingEdges()) {
            GraphNode* v = e->getEndNode();
            if (!v) continue;
            float alt = d + e->getWeight();
            auto itV = dist.find(v->getID());
            if (itV == dist.end() || alt < itV->second) {
                dist[v->getID()] = alt;
                prev[v->getID()] = u;
                frontier.push({ alt, v });
            }
        }
    }

    // No path → reconstruct fails.
    if (!dist.count(end->getID())) return path;

    GraphNode* cur = end;
    while (cur != nullptr) {
        path.push_back(cur);
        if (cur == start) break;
        auto itP = prev.find(cur->getID());
        cur = (itP != prev.end()) ? itP->second : nullptr;
    }
    if (path.empty() || path.back() != start) {
        path.clear();
        return path;
    }
    std::reverse(path.begin(), path.end());
    return path;
}
