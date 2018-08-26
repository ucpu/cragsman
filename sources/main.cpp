#include <exception>

#include <cage-core/core.h>
#include <cage-core/log.h>
#include <cage-core/math.h>
#include <cage-core/config.h>
#include <cage-core/assets.h>
#include <cage-core/utility/ini.h>
#include <cage-core/utility/hashString.h>

#include <cage-client/core.h>
#include <cage-client/window.h>
#include <cage-client/engine.h>
#include <cage-client/utility/engineProfiling.h>
#include <cage-client/utility/highPerformanceGpuHint.h>

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
		holder<loggerClass> log1 = newLogger();
		log1->filter.bind<logFilterPolicyPass>();
		log1->format.bind<logFormatPolicyConsole>();
		log1->output.bind<logOutputPolicyStdOut>();

		controlThread().timePerTick = 1000000 / 30;
		engineInitialize(engineCreateConfig());
		assets()->add(hashString("cragsman/cragsman.pack"));

		eventListener<bool()> windowCloseListener;
		windowCloseListener.bind<&windowClose>();
		window()->events.windowClose.attach(windowCloseListener);

		window()->title("Cragsman");
		window()->modeSetWindowed(windowFlags::Border | windowFlags::Resizeable);

		{
			holder<engineProfilingClass> engineProfiling = newEngineProfiling();
			engineProfiling->profilingMode = profilingModeEnum::None;
			engineProfiling->keyToggleFullscreen = 0;
			engineProfiling->screenPosition = vec2(0.5, 0.5);

			engineStart();
		}

		assets()->remove(hashString("cragsman/cragsman.pack"));
		engineFinalize();

		try
		{
			configSaveIni("cragsman.ini", "cragsman");
		}
		catch (...)
		{
			CAGE_LOG(severityEnum::Warning, "cragsman", "failed to save game configuration");
		}
		return 0;
	}
	catch (const cage::exception &e)
	{
		CAGE_LOG(severityEnum::Note, "exception", e.message);
		CAGE_LOG(severityEnum::Error, "exception", "caught cage exception in main");
	}
	catch (const std::exception &e)
	{
		CAGE_LOG(severityEnum::Note, "exception", e.what());
		CAGE_LOG(severityEnum::Error, "exception", "caught std exception in main");
	}
	catch (...)
	{
		CAGE_LOG(severityEnum::Error, "exception", "caught unknown exception in main");
	}
	return 1;
}
