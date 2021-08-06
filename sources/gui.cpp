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

		EntityManager *ents = engineGui()->entities();

		{ // best
			GuiTextComponent &t = ents->get(texts[0])->value<GuiTextComponent>();
			t.value = stringizer() + bestScore;
		}

		{ // current
			GuiTextComponent &t = ents->get(texts[1])->value<GuiTextComponent>();
			t.value = stringizer() + currentScore;
		}

		return false;
	}

	bool engineInitialize()
	{
		EntityManager *ents = engineGui()->entities();

		Entity *panel = nullptr;
		{ // panel
			Entity *wrapper = ents->createUnique();
			GuiScrollbarsComponent &sc = wrapper->value<GuiScrollbarsComponent>();
			panel = ents->createUnique();
			GuiParentComponent &parent = panel->value<GuiParentComponent>();
			parent.parent = wrapper->name();
			GuiPanelComponent &g = panel->value<GuiPanelComponent>();
			GuiLayoutTableComponent &lt = panel->value<GuiLayoutTableComponent>();
		}

		{ // best score
			{ // label
				Entity *e = ents->createUnique();
				GuiParentComponent &p = e->value<GuiParentComponent>();
				p.parent = panel->name();
				p.order = 1;
				GuiLabelComponent &l = e->value<GuiLabelComponent>();
				GuiTextComponent &t = e->value<GuiTextComponent>();
				t.value = "Best Score: ";
			}
			{ // value
				Entity *e = ents->createUnique();
				GuiParentComponent &p = e->value<GuiParentComponent>();
				p.parent = panel->name();
				p.order = 2;
				GuiLabelComponent &l = e->value<GuiLabelComponent>();
				GuiTextComponent &t = e->value<GuiTextComponent>();
				texts[0] = e->name();
			}
		}

		{ // current score
			{ // label
				Entity *e = ents->createUnique();
				GuiParentComponent &p = e->value<GuiParentComponent>();
				p.parent = panel->name();
				p.order = 3;
				GuiLabelComponent &l = e->value<GuiLabelComponent>();
				GuiTextComponent &t = e->value<GuiTextComponent>();
				t.value = "Current Score: ";
			}
			{ // value
				Entity *e = ents->createUnique();
				GuiParentComponent &p = e->value<GuiParentComponent>();
				p.parent = panel->name();
				p.order = 4;
				GuiLabelComponent &l = e->value<GuiLabelComponent>();
				GuiTextComponent &t = e->value<GuiTextComponent>();
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
