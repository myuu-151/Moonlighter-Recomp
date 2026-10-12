#include "game/PolyNavAgent.h"

#include <algorithm>

#include "unity/Mathf.h"

namespace game {

using unity::Vector2;
namespace Mathf = unity::Mathf;

namespace {

// PolyNavAgent.allAgents: the enabled agents, in the order they were enabled.
std::vector<PolyNavAgent*>& Agents()
{
    static std::vector<PolyNavAgent*> agents;
    return agents;
}

// Agents alive (a path can arrive after its agent is gone: the callback then does nothing).
std::vector<const PolyNavAgent*>& Alive()
{
    static std::vector<const PolyNavAgent*> alive;
    return alive;
}

bool IsAlive(const PolyNavAgent* a) { return std::find(Alive().begin(), Alive().end(), a) != Alive().end(); }

} // namespace

PolyNavAgent::PolyNavAgent()
{
    static long long nextId = 0;
    id_ = ++nextId;
    Alive().push_back(this);
}

PolyNavAgent::~PolyNavAgent()
{
    OnDisable();
    auto& alive = Alive();
    alive.erase(std::remove(alive.begin(), alive.end(), this), alive.end());
}

void PolyNavAgent::Awake()
{
    primeGoal_ = position();
}

void PolyNavAgent::OnEnable()
{
    Agents().push_back(this);
}

void PolyNavAgent::OnDisable()
{
    auto& agents = Agents();
    auto it = std::find(agents.begin(), agents.end(), this);
    if (it != agents.end()) agents.erase(it);
}

const std::vector<PolyNavAgent*>& PolyNavAgent::AllAgents() { return Agents(); }

Vector2 PolyNavAgent::position() const
{
    // Transform.position: the local position through each parent, nearest first.
    Vector2 p = localPosition;
    for (const Vector2& parent : parents) p = p + parent;
    return p;
}

void PolyNavAgent::setPosition(Vector2 value)
{
    // Transform.position's setter: the parent's InverseTransformPoint, from the root down.
    Vector2 p = value;
    for (auto it = parents.rbegin(); it != parents.rend(); ++it) p = p - *it;
    localPosition = p;
}

float PolyNavAgent::remainingDistance() const
{
    if (!hasPath()) return 0.0f;
    float num = unity::Distance(position(), activePath_[0]);
    const size_t n = activePath_.size();
    for (size_t i = 0; i < n; i++) num += unity::Distance(activePath_[i], activePath_[i == n - 1 ? i : i + 1]);
    return num;
}

Vector2 PolyNavAgent::movingDirection() const { return hasPath() ? unity::Normalized(velocity) : Vector2::zero(); }

float PolyNavAgent::currentSpeed() const { return unity::Magnitude(velocity); }

void PolyNavAgent::SetActivePath(const std::vector<Vector2>& path)
{
    activePath_ = path;
    if (!activePath_.empty()) activePath_.erase(activePath_.begin());
}

bool PolyNavAgent::SetDestination(Vector2 goal, std::function<void(bool)> callback)
{
    if (polyNav() == nullptr) return false;
    if (unity::SqrMagnitude(goal - primeGoal_) < Mathf::Epsilon) return true;
    reachedCallback_ = callback;
    primeGoal_ = goal;
    if (unity::SqrMagnitude(goal - position()) < stoppingDistance) {
        OnArrived();
        return true;
    }
    if (!polyNav()->PointIsValid(goal)) {
        if (closerPointOnInvalid) {
            SetDestination(polyNav()->GetCloserEdgePoint(goal), callback);
            return true;
        }
        OnInvalid();
        return false;
    }
    if (requests_ > 0) return true;
    requests_++;
    polyNav()->FindPath(position(), goal, MakePathCallback());
    return true;
}

PolyNav2D::PathCallback PolyNavAgent::MakePathCallback()
{
    PolyNavAgent* self = this;
    long long id = id_;
    return [self, id](const std::vector<Vector2>* path) {
        if (IsAlive(self) && self->id_ == id) self->SetPath(path);
    };
}

void PolyNavAgent::Stop()
{
    activePath_.clear();
    velocity = Vector2::zero();
    requests_ = 0;
    primeGoal_ = position();
}

void PolyNavAgent::SetPath(const std::vector<Vector2>* path)
{
    if (requests_ == 0) return;
    requests_--;
    if (path == nullptr || path->empty()) {
        OnInvalid();
        return;
    }
    SetActivePath(*path);
    if (OnNavigationStarted) OnNavigationStarted();
}

void PolyNavAgent::LateUpdate(float deltaTime)
{
    if (polyNav() == nullptr) return;
    if (!hasPath()) {
        Restrict();
        return;
    }
    if (remainingDistance() < slowingDistance) {
        accelerationValue_ = 0.0f;
        velocity = velocity + unity::Div(Arrive(nextPoint()), mass);
    } else {
        velocity = velocity + unity::Div(Seek(nextPoint()), mass);
        accelerationValue_ += accelerationRate * deltaTime;
        accelerationValue_ = Mathf::Clamp01(accelerationValue_);
        velocity = velocity * accelerationValue_;
    }
    velocity = Truncate(velocity, maxSpeed);
    if (roundCorners) velocity = velocity + unity::Lerp(Vector2::zero(), up * lookAheadDistance, remainingDistance() / slowingDistance);
    LookAhead();
    setPosition(position() + velocity * deltaTime);
    Restrict();
    if (rotateTransform) {
        Vector2 md = movingDirection();
        float target = (0.0f - Mathf::Atan2(md.x, md.y)) * 180.0f / 3.14159274f;
        rotationZ = Mathf::MoveTowardsAngle(rotationZ, target, rotateSpeed * deltaTime);
    }
    if (repath) {
        if (!polyNav()->CheckLOS(position(), nextPoint())) Repath();
        if (!hasPath()) {
            OnArrived();
            return;
        }
    }
    float num = (activePath_[activePath_.size() - 1] == nextPoint()) ? stoppingDistance : 0.05f;
    if (unity::SqrMagnitude(position() - nextPoint()) <= num) {
        activePath_.erase(activePath_.begin());
        if (!hasPath()) {
            OnArrived();
            return;
        }
        if (repath) Repath();
        if (OnNavigationPointReached) OnNavigationPointReached(position());
    }
    if (activePath_.size() > 1 && polyNav()->CheckLOS(position(), activePath_[1])) {
        activePath_.erase(activePath_.begin());
        if (OnNavigationPointReached) OnNavigationPointReached(position());
    }
}

void PolyNavAgent::Repath()
{
    if (requests_ <= 0) {
        requests_++;
        polyNav()->FindPath(position(), primeGoal_, MakePathCallback());
    }
}

void PolyNavAgent::OnArrived()
{
    Stop();
    if (reachedCallback_) reachedCallback_(true);
    if (OnDestinationReached) OnDestinationReached();
}

void PolyNavAgent::OnInvalid()
{
    Stop();
    if (reachedCallback_) reachedCallback_(false);
    if (OnDestinationInvalid) OnDestinationInvalid();
}

Vector2 PolyNavAgent::Seek(Vector2 pos) const
{
    Vector2 vec = unity::Normalized(pos - position()) * maxSpeed - velocity;
    return Truncate(vec, maxForce_);
}

Vector2 PolyNavAgent::Arrive(Vector2 pos) const
{
    Vector2 vector = pos - position();
    float magnitude = unity::Magnitude(vector);
    if (magnitude > 0.0f) {
        float a = magnitude / (decelerationRate * 0.3f);
        a = Mathf::Min(a, maxSpeed);
        vector = vector * (a / magnitude);
    }
    Vector2 vec = vector - velocity;
    return Truncate(vec, maxForce_);
}

void PolyNavAgent::LookAhead()
{
    if (lookAheadDistance <= 0.0f) return;
    float num = Mathf::Lerp(0.0f, lookAheadDistance, unity::Magnitude(velocity) / maxSpeed);
    Vector2 vector = position() + unity::Normalized(velocity) * num;
    if (!polyNav()->PointIsValid(vector)) velocity = velocity - (vector - position());
    if (!(avoidRadius > 0.0f)) return;
    const std::vector<PolyNavAgent*>& allAgents = Agents();
    for (size_t i = 0; i < allAgents.size(); i++) {
        PolyNavAgent* polyNavAgent = allAgents[i];
        if (!(polyNavAgent == this) && !(polyNavAgent->avoidRadius <= 0.0f)) {
            float num2 = polyNavAgent->avoidRadius + avoidRadius;
            float magnitude = unity::Magnitude(vector - polyNavAgent->position());
            Vector2 vector2 = unity::Normalized(vector - polyNavAgent->position()) * num2;
            Vector2 vector3 = unity::Lerp3(vector2, Vector2::zero(), magnitude / num2);
            velocity = velocity + vector3;
        }
    }
}

void PolyNavAgent::Restrict()
{
    if (restrict && !polyNav()->PointIsValid(position())) setPosition(polyNav()->GetCloserEdgePoint(position()));
}

Vector2 PolyNavAgent::Truncate(Vector2 vec, float max)
{
    if (unity::Magnitude(vec) > max) {
        vec = unity::Normalized(vec);
        vec = vec * max;
    }
    return vec;
}

} // namespace game
