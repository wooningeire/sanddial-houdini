#include "SIM_AreniteSolver.h"
#include <SIM/SIM_DopDescription.h>
#include <SIM/SIM_Object.h>
#include <SIM/SIM_ObjectArray.h>
#include <SIM/SIM_PRMShared.h>

const SIM_DopDescription* SIM_AreniteSolver::getDopDescription() {
    static PRM_Name thePassesName("passes", "Passes");
    static PRM_Default thePassesDefault(1);

    static PRM_Template theTemplates[] = {
        PRM_Template(PRM_INT, 1, &thePassesName, &thePassesDefault),
        PRM_Template()
    };

    static SIM_DopDescription theDopDescription(true,
                                                "arenite_solver", 
                                                "Arenite Solver", 
                                                "Solver", 
                                                classname(),
                                                theTemplates);
    return &theDopDescription;
}

SIM_AreniteSolver::SIM_AreniteSolver(const SIM_DataFactory* factory)
    : BaseClass(factory) {}

SIM_AreniteSolver::~SIM_AreniteSolver() {}

SIM_Solver::SIM_Result SIM_AreniteSolver::solveSingleObjectSubclass(SIM_Engine& engine,
                                                        SIM_Object& object,
                                                        SIM_ObjectArray& acknowledgedObjects,
                                                        const SIM_Time& timeStep,
                                                        bool isNewObject) {
    acknowledgedObjects.add(&object);
    return SIM_SOLVER_SUCCESS;
}
