#include <NodeRef.h>
#include <Node.h>

NodeRef NodeReferenceFactory::makeUniqueNodeReference(CNode* nodeToRef)
{
    return NodeRef{nodeToRef?nodeToRef->AddRef():nullptr,NodeRefDeleter()};
}