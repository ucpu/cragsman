
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

		entityManagerClass *ents = gui()->entities();

		{ // best
			GUI_GET_COMPONENT(text, t, ents->get(texts[0]));
			t.value = bestScore;
		}

		{ // current
			GUI_GET_COMPONENT(text, t, ents->get(texts[1]));
			t.value = currentScore;
		}

		return false;
	}

	bool engineInitialize()
	{
		entityManagerClass *ents = gui()->entities();

		entityClass *panel = nullptr;
		{ // panel
			entityClass *wrapper = ents->createUnique();
			GUI_GET_COMPONENT(scrollbars, sc, wrapper);
			panel = ents->createUnique();
			GUI_GET_COMPONENT(parent, parent, panel);
			parent.parent = wrapper->name();
			GUI_GET_COMPONENT(panel, g, panel);
			GUI_GET_COMPONENT(layoutTable, lt, panel);
		}

		{ // best score
			{ // label
				entityClass *e = ents->createUnique();
				GUI_GET_COMPONENT(parent, p, e);
				p.parent = panel->name();
				p.order = 1;
				GUI_GET_COMPONENT(label, l, e);
				GUI_GET_COMPONENT(text, t, e);
				t.value = "Best Score: ";
			}
			{ // value
				entityClass *e = ents->createUnique();
				GUI_GET_COMPONENT(parent, p, e);
				p.parent = panel->name();
				p.order = 2;
				GUI_GET_COMPONENT(label, l, e);
				GUI_GET_COMPONENT(text, t, e);
				texts[0] = e->name();
			}
		}

		{ // current score
			{ // label
				entityClass *e = ents->createUnique();
				GUI_GET_COMPONENT(parent, p, e);
				p.parent = panel->name();
				p.order = 3;
				GUI_GET_COMPONENT(label, l, e);
				GUI_GET_COMPONENT(text, t, e);
				t.value = "Current Score: ";
			}
			{ // value
				entityClass *e = ents->createUnique();
				GUI_GET_COMPONENT(parent, p, e);
				p.parent = panel->name();
				p.order = 4;
				GUI_GET_COMPONENT(label, l, e);
				GUI_GET_COMPONENT(text, t, e);
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
