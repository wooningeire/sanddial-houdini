#pragma once

#include <SOP/SOP_Node.h>
#include <GU/GU_DetailHandle.h>
#include <GA/GA_Types.h>
#include <map>

#include "AreniteGeometry.h"
#include "NormalsSolver.h"
#include "MpmSolver.h"
#include "WindSolver.h"
#include "WaterSolver.h"
#include "ErosionSolver.h"
#include "DepositionSolver.h"

class SOP_Sanddial : public SOP_Node {
public:
    static OP_Node* myConstructor(OP_Network* net, const char* name, OP_Operator* op);
    static PRM_Template myTemplateList[];

protected:
    SOP_Sanddial(OP_Network* net, const char* name, OP_Operator* op);
    virtual ~SOP_Sanddial();

    virtual OP_ERROR cookMySop(OP_Context& context) override;

private:
    /// Initialize AreniteGeometry from the input Houdini geometry.
    void initializeSimulation(const GU_Detail* inputGeo);

    /// Advance one simulation step using the Arenite pipeline.
    void advanceFrame(fpreal dt);

    /// Read Parameter Pane values and configure solvers.
    void loadParameters(fpreal t);

    /// Ensure the cache contains the result for the given frame.
    GU_DetailHandle getFrameResult(int frame, const GU_Detail* inputGeo, fpreal fps);

    // ── Simulation state ────────────────────────────────────────────────────
    AreniteGeometry  myGeo;
    NormalsSolver    myNormalsSolver;
    MpmSolver        myMpmSolver;
    WindSolver       myWindSolver;
    WaterSolver      myWaterSolver;
    ErosionSolver    myErosionSolver;
    DepositionSolver myDepositionSolver;

    std::map<int, GU_DetailHandle> myFrameCache;
    int myStartFrame = 1;
    GA_DataId myInputDataId = GA_INVALID_DATAID;
};
