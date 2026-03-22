#ifndef LSystem_H_
#define LSystem_H_

#include <string>
#include <vector>
#include <map>
#include "vec.h"


class LSystem
{
public:
    typedef std::pair<vec3, std::string> Geometry;
    typedef std::pair<vec3, vec3> Branch;

public:
    LSystem();
    ~LSystem() {}

    // Set/get inputs
    void loadProgram(const std::string& fileName);
    void loadProgramFromString(const std::string& program);
    void setDefaultAngle(float degrees);
    void setDefaultStep(float distance);
    void setDefaultAngleScale(float scale);
    void setDefaultStepScale(float scale);
    void setAngleNoiseScale(float scale);
    void setStepNoiseScale(float scale);
    void setAngleNoiseSeed(int seed);
    void setStepNoiseSeed(int seed);
    void setGravity(float gravity);

    float getDefaultAngle() const;
    float getDefaultStep() const;
    float getDefaultAngleScale() const;
    float getDefaultStepScale() const;
    float getAngleNoiseScale() const;
    float getStepNoiseScale() const;
    int getAngleNoiseSeed() const;
    int getStepNoiseSeed() const;
    float getGravity() const;
    const std::string& getGrammarString() const;

    // Iterate grammar
    const std::string& getIteration(unsigned int n);

    // Get geometry from running the turtle
    void process(unsigned int n, 
        std::vector<Branch>& branches); 
    void process(unsigned int n, 
        std::vector<Branch>& branches, 
        std::vector<Geometry>& models);

protected:
    void reset();
    void addProduction(std::string line);
    std::string iterate(const std::string& input);
    
    std::map<std::string, std::string> productions;
    std::vector<std::string> iterations;
    std::vector<std::pair<vec3,vec3>> bboxes;
    std::string current;
    float mDfltAngle;
    float mDfltStep;
    float mAngleScale;
    float mStepScale;
    float mAngleNoiseScale;
    float mStepNoiseScale;
    int mAngleNoiseSeed;
    int mStepNoiseSeed;
    float mGravity;
    std::string mGrammar;

    class Turtle
    {
    public:
        Turtle();
        Turtle(const Turtle& t);
        Turtle& operator=(const Turtle& t);

        void moveForward(float distance);
        void applyUpRot(float degrees);
        void applyLeftRot(float degrees);
        void applyForwardRot(float degrees);

        vec3 pos;
        vec3 up;
        vec3 forward;
        vec3 left;
    };
};

#endif