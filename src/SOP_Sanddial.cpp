#include "SOP_Sanddial.h"

#include <GU/GU_Detail.h>
#include <OP/OP_Operator.h>
#include <OP/OP_OperatorTable.h>
#include <OP/OP_AutoLockInputs.h>
#include <PRM/PRM_Include.h>

// Subtask 1.3: Parameter Pane integration
// Adding a simple template field to appear in Parameter Pane.
static PRM_Name prm_iterationsName("iterations", "Iterations");
static PRM_Default prm_iterationsDefault(10);

PRM_Template SOP_Sanddial::myTemplateList[] = {
    PRM_Template(PRM_INT, 1, &prm_iterationsName, &prm_iterationsDefault),
    PRM_Template()
};

OP_Node* SOP_Sanddial::myConstructor(OP_Network* net, const char* name, OP_Operator* op) {
    return new SOP_Sanddial(net, name, op);
}

SOP_Sanddial::SOP_Sanddial(OP_Network* net, const char* name, OP_Operator* op)
    : SOP_Node(net, name, op) {
    // This node needs 1 input
    mySopFlags.setNeedGuide1(true);
}

SOP_Sanddial::~SOP_Sanddial() {}

OP_ERROR SOP_Sanddial::cookMySop(OP_Context& context) {
    // We must lock our inputs before we try to access their geometry.
    // OP_AutoLockInputs will automatically unlock our inputs when we return.
    OP_AutoLockInputs inputs(this);
    if (inputs.lock(context) >= UT_ERROR_ABORT)
        return error();

    // Duplicate input geometry
    duplicateSource(0, context);

    // Evaluate parameter (Subtask 1.3)
    fpreal t = context.getTime();
    int iterations = evalInt("iterations", 0, t);

    // Pass-through for now.
    
    return error();
}
