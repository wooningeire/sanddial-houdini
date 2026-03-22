#include <UT/UT_DSOVersion.h>
#include <OP/OP_OperatorTable.h>
#include <SIM/SIM_DopDescription.h>
#include "SOP_Sanddial.h"
#include "SIM_AreniteSolver.h"

void newSopOperator(OP_OperatorTable* table) {
    table->addOperator(new OP_Operator(
        "sanddial", // Internal name
        "Sanddial", // UI name
        SOP_Sanddial::myConstructor, // How to build the SOP
        SOP_Sanddial::myTemplateList, // My parameters
        1, // Min # of sources
        1, // Max # of sources
        0, // Local variables
        0  // Flags
    ));
}

void initializeSIM(void*) {
    IMPLEMENT_DATAFACTORY(SIM_AreniteSolver);
}
