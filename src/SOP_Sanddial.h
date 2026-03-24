#pragma once

#include <SOP/SOP_Node.h>
#include <GU/GU_DetailHandle.h>
#include <GA/GA_Types.h>
#include <map>

class SOP_Sanddial : public SOP_Node {
public:
    static OP_Node* myConstructor(OP_Network* net, const char* name, OP_Operator* op);
    static PRM_Template myTemplateList[];

protected:
    SOP_Sanddial(OP_Network* net, const char* name, OP_Operator* op);
    virtual ~SOP_Sanddial();

    virtual OP_ERROR cookMySop(OP_Context& context) override;

private:
    /// Initialize particles from the input geometry's points.
    void initializeParticles(const GU_Detail* inputGeo, GU_Detail* outGeo);

    /// Advance one frame: apply gravity to all points.
    void advanceFrame(GU_Detail* geo, fpreal dt);

    /// Ensure the cache contains the result for the given frame,
    /// simulating forward from the latest cached frame if needed.
    GU_DetailHandle getFrameResult(int frame, const GU_Detail* inputGeo, fpreal fps);

    std::map<int, GU_DetailHandle> myFrameCache;
    int myStartFrame = 1;
    GA_DataId myInputDataId = GA_INVALID_DATAID;
};
