#pragma once

#include <SIM/SIM_SingleSolver.h>
#include "AreniteGeometry.h"
#include "NormalsSolver.h"
#include "MpmSolver.h"
#include "WindSolver.h"
#include "WaterSolver.h"
#include "ErosionSolver.h"
#include "DepositionSolver.h"

class SIM_AreniteSolver : public SIM_SingleSolver {
public:
    static const SIM_DopDescription* getDopDescription();

protected:
    explicit SIM_AreniteSolver(const SIM_DataFactory* factory);
    virtual ~SIM_AreniteSolver();

    virtual SIM_Solver::SIM_Result solveSingleObjectSubclass(SIM_Engine& engine,
        SIM_Object& object,
        SIM_ObjectArray& acknowledgedObjects,
        const SIM_Time& timeStep,
        bool isNewObject) override;

private:
    // ── Simulation state ────────────────────────────────────────────────────
    AreniteGeometry  myGeo;

    // ── Helper solvers (composed, called sequentially per step) ──────────
    NormalsSolver    myNormalsSolver;
    MpmSolver        myMpmSolver;
    WindSolver       myWindSolver;
    WaterSolver      myWaterSolver;
    ErosionSolver    myErosionSolver;
    DepositionSolver myDepositionSolver;

    DECLARE_STANDARD_GETCASTTOTYPE();
    DECLARE_DATAFACTORY(SIM_AreniteSolver,
                        SIM_SingleSolver,
                        "Arenite Solver",
                        getDopDescription());
};
