
#include "common.h"

#include <cage-core/log.h>
#include <cage-core/entities.h>
#include <cage-core/hashString.h>

#include <cage-client/core.h>
#include <cage-client/engine.h>
#include <cage-client/gui.h>

namespace
{
	sint32 bestScore;
	sint32 texts[2];

	bool engineUpdate()
	{
		sint32 currentScore = numeric_cast<sint32>(playerPosition[1] * 0.1);
		bestScore = max(currentScore, bestScore);

		entityManager *ents = gui()->entities();

		{ // best
			CAGE_COMPONENT_GUI(text, t, ents->get(texts[0]));
			t.value = bestScore;
		}

		{ // current
			CAGE_COMPONENT_GUI(text, t, ents->get(texts[1]));
			t.value = currentScore;
		}

		return false;
	}

	bool engineInitialize()
	{
		entityManager *ents = gui()->entities();

		entity *panel = nullptr;
		{ // panel
			entity *wrapper = ents->createUnique();
			CAGE_COMPONENT_GUI(scrollbars, sc, wrapper);
			panel = ents->createUnique();
			CAGE_COMPONENT_GUI(parent, parent, panel);
			parent.parent = wrapper->name();
			CAGE_COMPONENT_GUI(panel, g, panel);
			CAGE_COMPONENT_GUI(layoutTable, lt, panel);
		}

		{ // best score
			{ // label
				entity *e = ents->createUnique();
				CAGE_COMPONENT_GUI(parent, p, e);
				p.parent = panel->name();
				p.order = 1;
				CAGE_COMPONENT_GUI(label, l, e);
				CAGE_COMPONENT_GUI(text, t, e);
				t.value = "Best Score: ";
			}
			{ // value
				entity *e = ents->createUnique();
				CAGE_COMPONENT_GUI(parent, p, e);
				p.parent = panel->name();
				p.order = 2;
				CAGE_COMPONENT_GUI(label, l, e);
				CAGE_COMPONENT_GUI(text, t, e);
				texts[0] = e->name();
			}
		}

		{ // current score
			{ // label
				entity *e = ents->createUnique();
				CAGE_COMPONENT_GUI(parent, p, e);
				p.parent = panel->name();
				p.order = 3;
				CAGE_COMPONENT_GUI(label, l, e);
				CAGE_COMPONENT_GUI(text, t, e);
				t.value = "Current Score: ";
			}
			{ // value
				entity *e = ents->createUnique();
				CAGE_COMPONENT_GUI(parent, p, e);
				p.parent = panel->name();
				p.order = 4;
				CAGE_COMPONENT_GUI(label, l, e);
				CAGE_COMPONENT_GUI(text, t, e);
				texts[1] = e->name();
			}
		}

		return false;
	}

	class callbacksInitClass
	{
		eventListener<bool()> engineInitListener;
		eventListener<bool()> engineUpdateListener;
	public:
		callbacksInitClass()
		{
			engineInitListener.attach(controlThread().initialize);
			engineInitListener.bind<&engineInitialize>();
			engineUpdateListener.attach(controlThread().update);
			engineUpdateListener.bind<&engineUpdate>();
		}
	} callbacksInitInstance;
}
