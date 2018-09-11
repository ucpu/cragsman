
#include "common.h"

#include <cage-core/log.h>
#include <cage-core/entities.h>
#include <cage-core/utility/hashString.h>

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
			GUI_GET_COMPONENT(text, t, ents->getEntity(texts[0]));
			t.value = bestScore;
		}

		{ // current
			GUI_GET_COMPONENT(text, t, ents->getEntity(texts[1]));
			t.value = currentScore;
		}

		return false;
	}

	bool engineInitialize()
	{
		entityManagerClass *ents = gui()->entities();

		entityClass *panel = nullptr;
		{ // panel
			panel = ents->newUniqueEntity();
			GUI_GET_COMPONENT(groupBox, g, panel);
			GUI_GET_COMPONENT(layoutTable, lt, panel);
		}

		{ // best score
			{ // label
				entityClass *e = ents->newUniqueEntity();
				GUI_GET_COMPONENT(parent, p, e);
				p.parent = panel->getName();
				p.order = 1;
				GUI_GET_COMPONENT(label, l, e);
				GUI_GET_COMPONENT(text, t, e);
				t.value = "Best Score: ";
			}
			{ // value
				entityClass *e = ents->newUniqueEntity();
				GUI_GET_COMPONENT(parent, p, e);
				p.parent = panel->getName();
				p.order = 2;
				GUI_GET_COMPONENT(label, l, e);
				GUI_GET_COMPONENT(text, t, e);
				texts[0] = e->getName();
			}
		}

		{ // current score
			{ // label
				entityClass *e = ents->newUniqueEntity();
				GUI_GET_COMPONENT(parent, p, e);
				p.parent = panel->getName();
				p.order = 3;
				GUI_GET_COMPONENT(label, l, e);
				GUI_GET_COMPONENT(text, t, e);
				t.value = "Current Score: ";
			}
			{ // value
				entityClass *e = ents->newUniqueEntity();
				GUI_GET_COMPONENT(parent, p, e);
				p.parent = panel->getName();
				p.order = 4;
				GUI_GET_COMPONENT(label, l, e);
				GUI_GET_COMPONENT(text, t, e);
				texts[1] = e->getName();
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
