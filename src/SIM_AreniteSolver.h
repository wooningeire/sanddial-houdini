#pragma once

#include <SIM/SIM_SingleSolver.h>

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
    DECLARE_STANDARD_GETCASTTOTYPE();
    DECLARE_DATAFACTORY(SIM_AreniteSolver,
                        SIM_SingleSolver,
                        "Arenite Solver",
                        getDopDescription());
};
