// tools/gcsize: the program the whole-game size measurement links (gcsize.py). Not a game: it
// only names what the game's scenes reach, so the linker's --gc-sections keeps exactly that.
//
// What Unity reaches in a game's code is its MonoBehaviours, made by name from scenes and prefabs
// (the translation's gen::cs_AddComponent, bind's prefab builder's entry): through their vtables
// every message, every virtual and every method SendMessage, Invoke or an Animator event can name
// (cs_Message), and from those, everything they call. The name comes from a volatile so nothing
// is folded away.
#include "game.h"

volatile const char* gcsize_script = "BabySlimeBehaviour";

int main()
{
    ::UnityEngine::Component* c = ::gen::cs_AddComponent(nullptr, const_cast<const char*>(gcsize_script));
    return c != nullptr;
}
