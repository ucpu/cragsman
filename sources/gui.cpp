#include "common.h"

#include <cage-core/entities.h>
#include <cage-core/hashString.h>

#include <cage-engine/core.h>
#include <cage-engine/engine.h>
#include <cage-engine/gui.h>

namespace
{
	sint32 bestScore;
	sint32 texts[2];

	bool engineUpdate()
	{
		sint32 currentScore = numeric_cast<sint32>(playerPosition[1] * 0.1);
		bestScore = max(currentScore, bestScore);

		EntityManager *ents = gui()->entities();

		{ // best
			CAGE_COMPONENT_GUI(Text, t, ents->get(texts[0]));
			t.value = stringizer() + bestScore;
		}

		{ // current
			CAGE_COMPONENT_GUI(Text, t, ents->get(texts[1]));
			t.value = stringizer() + currentScore;
		}

		return false;
	}

	bool engineInitialize()
	{
		EntityManager *ents = gui()->entities();

		Entity *panel = nullptr;
		{ // panel
			Entity *wrapper = ents->createUnique();
			CAGE_COMPONENT_GUI(Scrollbars, sc, wrapper);
			panel = ents->createUnique();
			CAGE_COMPONENT_GUI(Parent, parent, panel);
			parent.parent = wrapper->name();
			CAGE_COMPONENT_GUI(Panel, g, panel);
			CAGE_COMPONENT_GUI(LayoutTable, lt, panel);
		}

		{ // best score
			{ // label
				Entity *e = ents->createUnique();
				CAGE_COMPONENT_GUI(Parent, p, e);
				p.parent = panel->name();
				p.order = 1;
				CAGE_COMPONENT_GUI(Label, l, e);
				CAGE_COMPONENT_GUI(Text, t, e);
				t.value = "Best Score: ";
			}
			{ // value
				Entity *e = ents->createUnique();
				CAGE_COMPONENT_GUI(Parent, p, e);
				p.parent = panel->name();
				p.order = 2;
				CAGE_COMPONENT_GUI(Label, l, e);
				CAGE_COMPONENT_GUI(Text, t, e);
				texts[0] = e->name();
			}
		}

		{ // current score
			{ // label
				Entity *e = ents->createUnique();
				CAGE_COMPONENT_GUI(Parent, p, e);
				p.parent = panel->name();
				p.order = 3;
				CAGE_COMPONENT_GUI(Label, l, e);
				CAGE_COMPONENT_GUI(Text, t, e);
				t.value = "Current Score: ";
			}
			{ // value
				Entity *e = ents->createUnique();
				CAGE_COMPONENT_GUI(Parent, p, e);
				p.parent = panel->name();
				p.order = 4;
				CAGE_COMPONENT_GUI(Label, l, e);
				CAGE_COMPONENT_GUI(Text, t, e);
				texts[1] = e->name();
			}
		}

		return false;
	}

	class Callbacks
	{
		EventListener<bool()> engineInitListener;
		EventListener<bool()> engineUpdateListener;
	public:
		Callbacks()
		{
			engineInitListener.attach(controlThread().initialize);
			engineInitListener.bind<&engineInitialize>();
			engineUpdateListener.attach(controlThread().update);
			engineUpdateListener.bind<&engineUpdate>();
		}
	} callbacksInstance;
}
