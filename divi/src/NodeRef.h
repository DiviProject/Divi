#ifndef NODE_REF_H
#define NODE_REF_H
#include <memory>
#include <Node.h>
struct NodeRefDeleter
{
    void operator()(CNode* node)
    {
        if(node) node->Release();
    }
};
typedef std::unique_ptr<CNode,NodeRefDeleter> NodeRef;
class NodeReferenceFactory
{
public:
    static NodeRef makeUniqueNodeReference(CNode* nodeToRef);
};
#endif// NODE_REF_H