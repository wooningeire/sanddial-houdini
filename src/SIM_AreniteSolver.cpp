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

    fpreal dt = static_cast<fpreal>(timeStep);

    // ── Arenite simulation loop (one step) ──────────────────────────────
    // 0. Reset per-step transient data.
    myGeo.resetStepData();

    // 1. Compute stress tensors via MLS-MPM.
    myMpmSolver.solve(myGeo, dt);

    // 2. Recalculate particle normals (surface detection + KNN).
    myNormalsSolver.solve(myGeo);

    // 3. Compute wind erosion (deflation + abrasion).
    myWindSolver.solve(myGeo, dt);

    // 4. Compute fluvial erosion (FastFlow discharge).
    myWaterSolver.solve(myGeo, dt);

    // 5. Combine erosion and update viabilities.
    myErosionSolver.solve(myGeo, dt);

    // 6. Deposit eroded particles via gravity routing.
    myDepositionSolver.solve(myGeo, dt);

    return SIM_SOLVER_SUCCESS;
}
