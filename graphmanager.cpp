#include "graphmanager.h"

#include "graphnode.h"
#include "edge.h"

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
