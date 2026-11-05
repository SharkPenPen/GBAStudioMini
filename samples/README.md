# 示例

三个 demo 都是编辑器数据：场景、对象、动画、规则、HUD 在 JSON/资源里；
运行时没有为它们新增任何玩法代码。

## Bee Swatter / 打蜜蜂

工程：`samples/bee`

- 打开 `samples/bee/project.json`，F5；
- 左右键移动，X 发射；
- 蜜蜂从右往左飞，子弹命中后消失。

## Platformer / 平台跳跃

工程：`samples/platformer`

- 打开 `samples/platformer/project.json`，F5；
- 左右键移动，Z 跳跃；
- 有重力、地面/平台碰撞、出口区域；
- 走到右边门进入 `done` 场景。

验证点：重力、跳跃条件、图块碰撞、场景切换、HUD。

## Topdown / 俯视探索

工程：`samples/topdown`

- 打开 `samples/topdown/project.json`，F5；
- 方向键四向移动；
- 靠近绿色 NPC 显示对话；
- 走到右下门进入 `room2`，再走门回 `room1`。

验证点：对象规则、区域触发、对话、场景切换、HUD。

## 直接运行 p3d

~~~powershell
.\build\host\bin\PocketLoom-Play.exe --p3d samples\bee\bee.p3d
.\build\host\bin\PocketLoom-Play.exe --p3d samples\platformer\game.p3d
.\build\host\bin\PocketLoom-Play.exe --p3d samples\topdown\game.p3d
~~~

## 重新导出

~~~powershell
.\build\host\bin\ploom.exe --open samples\bee\project.json --export-p3d samples\bee\bee.p3d
.\build\host\bin\ploom.exe --open samples\platformer\project.json --export-p3d samples\platformer\game.p3d
.\build\host\bin\ploom.exe --open samples\topdown\project.json --export-p3d samples\topdown\game.p3d
~~~

## 最小渲染 demo

`samples/demo/demo.p3d` 只有地砖、玩家方块和 HUD：

~~~powershell
.\build\host\bin\PocketLoom-Play.exe --p3d samples\demo\demo.p3d
~~~
