#include "common.h"

#include <cage-core/entities.h>
#include <cage-core/hashString.h>

#include <cage-engine/scene.h>
#include <cage-engine/guiBuilder.h>
#include <cage-simple/engine.h>

namespace
{
	sint32 bestScore;

	const auto engineUpdateListener = controlThread().update.listen([]() {
		sint32 currentScore = numeric_cast<sint32>(playerPosition[1] * 0.1);
		bestScore = max(currentScore, bestScore);
		EntityManager *ents = engineGuiEntities();
		ents->get(1)->value<GuiTextComponent>().value = Stringizer() + bestScore;
		ents->get(2)->value<GuiTextComponent>().value = Stringizer() + currentScore;
	});

	const auto engineInitListener = controlThread().initialize.listen([]() {
		Holder<GuiBuilder> g = newGuiBuilder(engineGuiEntities());
		auto _1 = g->alignment(Vec2(0));
		auto _2 = g->panel();
		auto _3 = g->verticalTable(2);
		g->label().text("Best Score: ");
		g->setNextName(1).label().text("");
		g->label().text("Current Score: ");
		g->setNextName(2).label().text("");
	});
}
