#pragma once
// PolyNavAgent.cs, literally: SetDestination (primeGoal, one request at a time,
// closerPointOnInvalid), Stop, and LateUpdate's steering: Seek / Arrive (slowingDistance),
// the acceleration ramp, mass, maxSpeed truncation, LookAhead (the map and the other
// agents' avoidRadius), the move, Restrict, waypoints reached, the line-of-sight shortcut
// and repath. The map is PolyNav2D::current (game/PolyNav.h, which also says where in the
// frame the paths arrive).
//
// The agent's position is its transform's: `localPosition` under `parents` (translations
// from its parent up to the root, as Transform.position reads and writes it through them).
// Its rotation is only `rotationZ` (localEulerAngles.z for rotateTransform) and `up`
// (transform.up for roundCorners).

#include <functional>
#include <vector>

#include "game/PolyNav.h"
#include "unity/Vector2.h"

namespace game {

class PolyNavAgent {
public:
    float maxSpeed = 3.5f;
    float mass = 20.0f;
    float stoppingDistance = 0.1f;
    float slowingDistance = 1.0f;
    float accelerationRate = 2.0f;
    float decelerationRate = 2.0f;
    bool rotateTransform = false;
    float rotateSpeed = 350.0f;
    float avoidRadius = 0.0f;
    float lookAheadDistance = 1.0f;
    bool repath = true;
    bool restrict = true;
    bool closerPointOnInvalid = true;
    bool roundCorners = false;
    unity::Vector2 velocity;

    // The transform.
    unity::Vector2 localPosition;
    std::vector<unity::Vector2> parents; // parents' local positions, nearest first
    float rotationZ = 0.0f;
    unity::Vector2 up{0.0f, 1.0f};

    std::function<void()> OnNavigationStarted, OnDestinationReached, OnDestinationInvalid;
    std::function<void(unity::Vector2)> OnNavigationPointReached;

    PolyNavAgent();
    PolyNavAgent(const PolyNavAgent&) = delete;
    PolyNavAgent& operator=(const PolyNavAgent&) = delete;
    ~PolyNavAgent();

    // Awake (primeGoal = the position) and OnEnable (joins allAgents).
    void Awake();
    void OnEnable();
    void OnDisable();
    static const std::vector<PolyNavAgent*>& AllAgents();

    unity::Vector2 position() const;
    void setPosition(unity::Vector2 value);

    bool SetDestination(unity::Vector2 goal, std::function<void(bool)> callback = nullptr);
    void Stop();
    void LateUpdate(float deltaTime);

    std::vector<unity::Vector2>& activePath() { return activePath_; }
    unity::Vector2 primeGoal() const { return primeGoal_; }
    bool pathPending() const { return requests_ > 0; }
    bool hasPath() const { return !activePath_.empty(); }
    unity::Vector2 nextPoint() const { return hasPath() ? activePath_[0] : position(); }
    float remainingDistance() const;
    unity::Vector2 movingDirection() const;
    float currentSpeed() const;
    // The private state, for a replay that starts from a recorded agent (its fields as the
    // game's agent has them) and compares them.
    float accelerationValue() const { return accelerationValue_; }
    int requests() const { return requests_; }
    void RestoreState(unity::Vector2 primeGoal, float accelerationValue, int requests, const std::vector<unity::Vector2>& activePath)
    {
        primeGoal_ = primeGoal;
        accelerationValue_ = accelerationValue;
        requests_ = requests;
        activePath_ = activePath;
    }

private:
    PolyNav2D* polyNav() const { return PolyNav2D::current; }
    void SetActivePath(const std::vector<unity::Vector2>& path); // the activePath setter
    void SetPath(const std::vector<unity::Vector2>* path);
    PolyNav2D::PathCallback MakePathCallback();
    void Repath();
    void OnArrived();
    void OnInvalid();
    unity::Vector2 Seek(unity::Vector2 pos) const;
    unity::Vector2 Arrive(unity::Vector2 pos) const;
    void LookAhead();
    void Restrict();
    static unity::Vector2 Truncate(unity::Vector2 vec, float max);

    long long id_ = 0; // (an agent's paths find it by address and id)
    float maxForce_ = 100.0f;
    int requests_ = 0;
    std::vector<unity::Vector2> activePath_;
    unity::Vector2 primeGoal_;
    float accelerationValue_ = 0.0f;
    std::function<void(bool)> reachedCallback_;
};

} // namespace game
