#pragma once
// The room's critters in an enemy replay (tests/enemy_replay.h): the Buggies that crawl about
// (Wormy, Beetly: a Dynamic Rigidbody2D with a trigger box, moved by their own DOMove with their
// own Random draws, which the replay resyncs). An enemy's attack trigger enters their box
// (enemy_FlyingKamikazeGolem.jsonl, frame 466: OnTriggerEnter2D on Wormy(Clone), where the golem
// strikes the hero). Their moves aren't the replay's to make:
//   - a recording with their positions (CritterOracle.cs: the start's "critters", each fixed
//     step's "csteps") has each one's box in the world, a Dynamic body placed where its transform
//     was at each step's start (as Unity's sync of a moved transform places it), so the enemies'
//     hits on them are compared with the others ("attack trigger: OnTriggerEnter2D (whose, on the
//     hero or a collider)");
//   - one without (the recordings before CritterOracle.cs): the hits the game reports on a
//     critter (an object of the room on a Dynamic body that isn't one of the recorded enemies)
//     can't be made, and fail on a row of their own ("... on the room's critters (their moves
//     unrecorded: record again)") until the recording is made again.
// Included by enemy_replay.h inside its anonymous namespace (its helpers: Value, V2, Bits).

struct CritterReplay {
    struct Critter {
        std::string name;
        unity::Rigidbody2D* body = nullptr; // null: gone at the start
        Vector2 offset;                     // the box's (as the start's scale made it)
        float scaleX = 1.0f;
    };
    bool recorded = false;            // the recording has "critters"
    std::vector<Critter> critters;    // (recorded)
    std::vector<std::string> names;   // the room's objects on Dynamic bodies that aren't the enemies'
    bool flipNoted = false;

    static float Radians(float degrees) { return static_cast<float>(static_cast<double>(degrees) * 3.14159265358979323846 / 180.0); }

    void Setup(const Value& start, const std::vector<Value>& colliders, const std::vector<Value>& enemyLines, unity::PhysicsWorld& world)
    {
        const std::string roomPath = "Dungeon/" + start["room"].s + "/";
        auto underEnemy = [&](const std::string& path) {
            for (const Value& e : enemyLines) {
                const Value* n = e.Find("name");
                if (n && n->IsString() && path.compare(0, n->s.size(), n->s) == 0) return true;
            }
            return false;
        };
        for (const Value& c : colliders) {
            const std::string& path = c["path"].s;
            if (!c.Find("body") || c["body"].s != "Dynamic" || path.compare(0, roomPath.size(), roomPath) != 0 || underEnemy(path)) continue;
            const std::string name = path.substr(path.rfind('/') + 1);
            if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
        }
        const Value* list = start.Find("critters");
        if (!list) return;
        recorded = true;
        for (const Value& c : list->arr) {
            Critter k;
            if (c.Find("gone") && c["gone"].b) {
                critters.push_back(k);
                continue;
            }
            k.name = c["path"].s.substr(c["path"].s.rfind('/') + 1);
            if (std::find(names.begin(), names.end(), k.name) == names.end()) names.push_back(k.name);
            unity::Rigidbody2DDef def;
            def.bodyType = c["body"].s == "Kinematic" ? unity::RigidbodyType2D::Kinematic
                           : c["body"].s == "Static"  ? unity::RigidbodyType2D::Static
                                                      : unity::RigidbodyType2D::Dynamic;
            def.mass = Bits(c["mass"]);
            def.gravityScale = 0.0f;
            // (Its box as Unity scales it: the offset by the lossy scale, the size by its size.)
            const Vector2 scale = V2(c["scale"]);
            k.scaleX = scale.x;
            k.offset = {V2(c["offset"]).x * scale.x, V2(c["offset"]).y * scale.y};
            const Vector2 size = {V2(c["size"]).x * std::fabs(scale.x), V2(c["size"]).y * std::fabs(scale.y)};
            k.body = &world.AddBody(def, V2(c["pos"]), static_cast<int>(c["layer"].AsInt()), k.offset, size);
            b2Body* b = k.body->Body();
            for (b2Fixture* f = b->GetFixtureList(); f; f = f->GetNext()) f->SetSensor(c["trigger"].b);
            b->SetTransform(b->GetPosition(), Radians(Bits(c["rot"])));
            b->SetActive(c["active"].b && c["enabled"].b);
            critters.push_back(k);
        }
    }

    // A fixed step's start: each critter where the game had it ("csteps" row k), its box there
    // or not.
    void BeforeStep(const Value& frame, size_t k, unity::PhysicsWorld& world)
    {
        const Value* steps = frame.Find("csteps");
        if (!recorded || !steps || k >= steps->size()) return;
        for (const Value& r : (*steps)[k].arr) {
            const size_t i = static_cast<size_t>(r[0].AsInt());
            if (i >= critters.size() || !critters[i].body) continue;
            Critter& c = critters[i];
            b2Body* b = c.body->Body();
            const bool on = r[3].b && r[4].b;
            if (b->IsActive() != on) b->SetActive(on);
            if (!on) continue;
            if (!flipNoted && (Bits(r[6]) < 0.0f) != (c.scaleX < 0.0f) && c.offset.x != 0.0f) {
                flipNoted = true;
                std::printf("note: critter %s turned (lossyScale x) with an offset box: its box keeps the start's side\n", c.name.c_str());
            }
            const float angle = Radians(Bits(r[5]));
            if (b->GetAngle() != angle) b->SetTransform(b->GetPosition(), angle);
            world.SyncTransform(*c.body, {Bits(r[1]), Bits(r[2])});
        }
    }

    bool IsCritterName(const std::string& name) const { return std::find(names.begin(), names.end(), name) != names.end(); }
};
