#include <cage-core/core.h>
#include <cage-core/logger.h>
#include <cage-core/math.h>
#include <cage-core/config.h>
#include <cage-core/assetManager.h>
#include <cage-core/ini.h>
#include <cage-core/hashString.h>

#include <cage-engine/core.h>
#include <cage-engine/window.h>
#include <cage-engine/engine.h>
#include <cage-engine/engineStatistics.h>
#include <cage-engine/highPerformanceGpuHint.h>
#include <cage-engine/fullscreenSwitcher.h>

using namespace cage;

namespace
{
	bool windowClose()
	{
		engineStop();
		return true;
	}
}

int main(int argc, const char *args[])
{
	try
	{
		configSetBool("cage/config/autoSave", true);
		Holder<Logger> log1 = newLogger();
		log1->format.bind<logFormatConsole>();
		log1->output.bind<logOutputStdOut>();

		engineInitialize(EngineCreateConfig());
		controlThread().updatePeriod(1000000 / 30);
		engineAssets()->add(HashString("cragsman/cragsman.pack"));

		EventListener<bool()> windowCloseListener;
		windowCloseListener.bind<&windowClose>();
		engineWindow()->events.windowClose.attach(windowCloseListener);
		engineWindow()->title("Cragsman");

		{
			Holder<FullscreenSwitcher> fullscreen = newFullscreenSwitcher({});
			Holder<EngineStatistics> engineStatistics = newEngineStatistics();
			engineStatistics->statisticsScope = EngineStatisticsScopeEnum::None;
			engineStatistics->screenPosition = Vec2(0.5, 0.5);

			engineStart();
		}

		engineAssets()->remove(HashString("cragsman/cragsman.pack"));
		engineFinalize();
		return 0;
	}
	catch (...)
	{
		detail::logCurrentCaughtException();
	}
	return 1;
}
