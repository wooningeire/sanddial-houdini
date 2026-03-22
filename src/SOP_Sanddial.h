#pragma once

#include <SOP/SOP_Node.h>

class SOP_Sanddial : public SOP_Node {
public:
    static OP_Node* myConstructor(OP_Network* net, const char* name, OP_Operator* op);
    static PRM_Template myTemplateList[];

protected:
    SOP_Sanddial(OP_Network* net, const char* name, OP_Operator* op);
    virtual ~SOP_Sanddial();

    virtual OP_ERROR cookMySop(OP_Context& context) override;
};
