#ifndef TEST_IMPORT_DATA_H
#define TEST_IMPORT_DATA_H

static const char *SAMPLE_LUA = R"LUA(
--[[ длинный
комментарий ]]
setTime("17:30")  -- время

local stU = "U"   -- константа
local unused = "T"

-- Первый поезд
t1 = TrainData.new()
t1.name = "34 \"быстрый\""
t1.config = "poezd"
t1.traj = "T"
t1.coord = 100.0
t1.dir = -1
t1.auto = true
setTrain(t1)

-- Поезд с неизвестным составом
t2 = TrainData.new()
t2.name = "X"
t2.config = "no-such-train"
t2.traj = "T"
t2.coord = 300
t2.dir = 1
setTrain(t2)

-- Отправление
setTimeTrigger("17:31", actionBuildTrainRoute(t1.traj, stU, 1))

-- Действие функцией
function dep()
	buildTrainRoute("T", "U", 1)
	openSignal("s")
	switchFwd("s")
end
setOnTrajBusyByTrainTrigger("T", t1.name, dep)

function wrap()
	setPostEventTimeTrigger("+00:01:20", actionReverseTrain("X"))
end
setOnTrajFreeTrigger("U", wrap)

-- Своя логика
function custom()
	if true then
		logMessage("hi")
	end
end
setTrigger(custom)
setOnTrajBusyTrigger("U", custom)
setAutoApproachTrigger("T", "U", 1)
)LUA";

#endif
