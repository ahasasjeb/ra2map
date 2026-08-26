# -*- coding: utf-8 -*-
"""Insert Simplified Chinese language sections into FA/FS language INI files."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


CHINESE_HEADER = """
[ChineseHeader]
Name=简体中文
Version=1
ExtensionName=CHS
"""

# Shared strings used by both editors (TS-oriented defaults). RA2 overrides are separate.
CHINESE_STRINGS = r"""
[Chinese-Strings]
; messages for message boxes
Err_InsufficientResources=由于系统资源不足，至少有一个对话框未能创建。建议你重新启动 Windows。程序现在将退出……提示：启动 %9 前请先关闭其他程序，重启 Windows 也可能有帮助。
Err_TSNotInstalled=泰伯利亚之日未正确安装（无法加载 TibSun.mix）
Err_CreateErr=无法创建窗口。请先尝试关闭其他窗口。
MainDialogExitQuestion=确定要退出程序吗？未保存的地图更改将会丢失！
MainDialogExitQuestionCap=退出 %9
AttachMapToShell=.map 和/或 .mpr 文件当前关联到其他程序。是否让 %9 在双击这些文件时打开它们？
AddHouse=请输入新阵营的 ID（例如 GDI 或 Nod）：
AddHouseCap=添加阵营
DeleteHouse=确定要删除阵营 %1 吗？
DeleteHouseCap=删除阵营
RestartNeeded=要使全部更改生效，需要重新启动 %9
ReInitPic=需要重新加载图形。这可能需要一些时间，请稍候。
ReInitPicCap=正在重新加载图形
StrChangeHeight=请输入要应用到每个格子的高度差。该值必须介于 %1 和 %2 之间
StrChangeHeightCap=更改高度
StrChangeHeightErr=无法更改地图高度，因为这会超出高度限制。
StrChangeHeightErrCap=错误
ExplainEasyView=%9 会自动以新手模式启动。该模式下部分高级编辑功能不可用，但在专业模式中可以使用。要启用专业模式，请在“选项”菜单中关闭“新手模式”。
ExplainEasyViewCap=新手模式
FileSaved=地图已保存为“%1”

; general strings
OK=确定
Cancel=取消
None=无
Yes=是
No=否
Next=下一步
Browse=浏览

; YRM
NeedsYR=游玩此地图需要安装尤里的复仇。

; name replacements
N_AMRADR=美国空军指挥部
N_SENGINEER=苏军工程师
N_ENGINEER=盟军工程师
N_YENGINEER=尤里工程师
N_SCHD=已着陆的攻城直升机
N_GACSPH=超时空传送仪
N_CALOND04=伦敦议会大厦
N_CALOND05=伦敦大本钟
N_CATRAN03=运输要塞

; saving dialog
SAVEDLG_FILETYPES=TS 地图|*.mpr%8*.map|TS 多人地图|*.mpr|TS 单人地图|*.map|

; tool tip strings
TT_TriggerHouse=指定该触发器所作用的阵营

; error strings for map validation
MV_NoMap=这不是一张地图
MV_NoBasic=缺少常规设置
MV_NoName=地图没有名称（编辑->常规）
MV_PackMissing=缺少一个或多个数据包（如 IsoMapPack5 或 OverlayPack）。泰伯利亚之日可能无法接受该地图。
MV_>100Waypoint=地图中至少有一个路径点的 ID 大于 99
MV_HousesButNoPlayer=已定义阵营，但人类玩家未正确设置
MV_HousesInMultiplayer=这似乎是一张定义了阵营的多人地图
MV_HousesNoWaypoints=多人地图需要路径点 0-7 作为起始位置
MV_TriggerMissing=缺少触发器 %1（由 %2 %3 引用）
MV_TaskForceMissing=缺少特遣部队 %1（由小队类型 %2 引用）
MV_ScripttypeMissing=缺少脚本类型 %1（由小队类型 %2 引用）
MV_TagMissing=缺少标签 %1（由 %2 %3 引用）
MV_OfficialYes=Official 已设为是 - 已关闭自动地图传输（保存为 MMX 时可忽略此警告）
MV_Not8Waypoints=仅适用于 1.005 之前的红色警戒2：Official 设为否，但路径点 0-7 并不齐全（可能是随机出生点，保存为 MMX 时可忽略）
MV_TubeCounterpartMissing=隧道管 %5（%1, %2）->（%3, %4）没有从（%3, %4）->（%1, %2）的对应管。\n这会在游戏中导致不可预知的错误和崩溃。\n对应管不必走同一路径，但起点和终点必须对调。
MV_TubeStartNotUnique=隧道管 %5（%1, %2）->（%3, %4）与另外 %6 条隧道管起点相同。\n这会在游戏中导致不可预知的错误和崩溃。\n每个格子只能有一条隧道管作为起点。
MV_TubeEndNotUnique=隧道管 %5（%1, %2）->（%3, %4）与另外 %6 条隧道管终点相同。\n这会在游戏中导致不可预知的错误和崩溃。\n每个格子只能有一条隧道管作为终点。
MV_TubeInvalidCounterpartEnd=隧道管 %5（%1, %2）->（%3, %4）有 %6 条其他隧道管以本管起点为终点，但本管终点与那些管的起点不匹配。\n这会在游戏中导致不可预知的错误和崩溃。\n对应管必须同时对调起点和终点。
MV_TubeInvalidCounterpartStart=隧道管 %5（%1, %2）->（%3, %4）有 %6 条其他隧道管以本管终点为起点，但本管起点与那些管的终点不匹配。\n这会在游戏中导致不可预知的错误和崩溃。\n对应管必须同时对调起点和终点。

; main dialog strings
MainDialogCaption=%9
MainDialogCaptionRA2=%9
NoMapLoaded=未加载地图
NewMap=新地图
FileNewHelp=启动新建地图向导
FileOpenHelp=打开已有地图
FileSaveHelp=保存地图
FileSaveAsHelp=将地图另存为其他文件名
FileCheckMapHelp=检查地图中可能存在的错误
FileImportModHelp=将 *.rul 模组导入地图
FileQuitHelp=退出 %9
FileRunTiberianSunHelp=启动 泰伯利亚之日
HelpInfoHelp=显示关于 %9 的信息
OptionsShowMapViewHelp=显示等距地图视图
TabBasic=常规
TabSingleplayerSettings=附加
TabMap=地图
TabLighting=光照
TabSpecial=特殊
TabHouses=阵营
TabTaskForces=特遣部队
TabScriptTypes=脚本
TabTeamTypes=小队类型
TabTriggers=触发器
TabTags=标签
TabAITriggers=AI 触发器
TabAITriggerEnable=启用 AI 触发器
TabOther=全部

; new map dialog
NewMapDesc=目前仍需要指定一张地图，以提供地面所需的地图包数据
NewMapBrowse=浏览
NewMapMultiplayer=多人地图
NewMapPrepareStandardHouses=准备标准阵营
NewMapSetAutoProduction=添加自动生产触发器
NewMapPlayerHouse=玩家阵营
NewMapImportOptions=导入选项
NewMapImportTrees=导入树木
NewMapImportOverlay=导入覆盖层
NewMapImportUnits=导入单位
NewMapCap=新建地图向导
NewMapStep1Cap=新建地图 - 第 1 步
NewMapStep2Cap=新建地图 - 第 2 步
NewMapStep3Cap=新建地图 - 第 3 步
NewMapStep4Cap=新建地图 - 第 4 步
NewMapTypeDesc=请选择要创建单人地图还是多人地图。多人地图可用于遭遇战和多人游戏。
NewMapTypeSingle=单人地图（仅建议能使用专业模式的有经验用户）
NewMapTypeMulti=多人地图
NewMapCreateDesc=请选择从零创建全新地图，还是导入已有地图（注意：无法导入红色警戒2随机地图生成器，或泰伯利亚之日 1.15 及以上版本随机生成的地图）
NewMapCreateNew=创建全新地图
NewMapCreateImport=导入已有地图或位图（BMP）
NewMapCreateAITriggers=启用 AI 触发器（AI 表现更好）
NewMapSpDesc=请选择人类玩家。建议保持其他选项不变。
NewMapCreateNewDesc=请在此选择尺寸、地形风格和起始高度。为获得最佳效果，尺寸建议小于 100x100。
NewMapWidth=宽度：
NewMapHeight=高度：
NewMapTheater=地形风格：
NewMapStartingHeight=起始高度：
NewMapImportDesc=请选择要导入的地图。不会导入触发器或阵营。位图会被缩小到合适尺寸。目前位图始终会转换为温带地形地图。

; map-validator dialog
MapValidatorProblemsFound=发现以下可能的问题：
MapValidatorCap=检查地图

; basic dialog
BasicDesc=注意：部分设置可能会被忽略。
BasicName=名称：
BasicNextScenario=下一关：
BasicAltNextScenario=备选下一关：
BasicNewIniFormat=新 INI 格式：
BasicCarryOverCap=CarryOverCap：
BasicEndOfGame=游戏结束：
BasicSkipScore=跳过得分统计：
BasicOneTimeOnly=仅一次：
BasicSkipMapSelect=跳过地图选择：
BasicOfficial=官方：
BasicIgnoreGlobalAITriggers=忽略全局 AI 触发器：
BasicTruckCrate=摧毁卡车掉落箱子：
BasicTrainCrate=摧毁火车掉落箱子：
BasicPercent=百分比（资金？）：
BasicMultiplayerOnly=仅多人：
BasicTiberiumGrowthEnabled=泰矿生长：
BasicVeinGrowthEnabled=脉络生长：
BasicIceGrowthEnabled=冰面生长：
BasicTiberiumDeathToVisceroid=死于泰矿时生成内脏怪：
BasicFreeRadar=免费雷达：
BasicInitTime=初始时间：
BasicAddOnNeeded=需要资料片：

; mapinfo dialog
MapDesc=常规地图属性：
MapSizeFrame=地图尺寸
MapSize=整张地图的尺寸，用于 MapPack。
MapVisibleSizeFrame=可见区域
MapVisibleSize=地图可见区域。格式：左, 上, 宽, 高。
MapTheater=地形风格：

; singleplayer basics dialog
SingleplayerDesc=与单人地图相关的附加设置。
SingleplayerStartingDropships=开局空投舰数量：
SingleplayerCarryOverMoney=继承资金：
SingleplayerTimerInherit=继承计时器：
SingleplayerFillSilos=填满筒仓：
SingleplayerMovies=影片
SingleplayerIntro=开场：
SingleplayerBrief=简报：
SingleplayerWin=胜利：
SingleplayerLose=失败：
SingleplayerAction=行动：
SingleplayerPostScore=得分画面之后：
SingleplayerPreMapSelect=地图选择画面之前：

; houses dialog
HousesDesc=阵营代表不同的玩家，包括 AI 和人类。如果这是多人地图，请不要在这里新建阵营！如果这是单人地图且还没有任何阵营，请先点击“标准阵营”，再按需创建额外阵营，然后选择人类玩家（别忘了为该阵营启用“玩家控制”）！
HousesPlayerHouse=人类玩家阵营：
HousesHouse=当前阵营：
HousesIQ=智商：
HousesEdge=地图边缘：
HousesSide=阵营方：
HousesColor=颜色：
HousesAllies=盟友：
HousesAlliesHelp=按如下格式列出所有盟友阵营：GDI,Nod,Neutral
HousesCredits=资金（x100）：
HousesActsLike=行为类似
HousesNodeCount=节点数量：
HousesTechlevel=科技等级：
HousesBuildActivity=建造活跃度（%）：
HousesPlayerControl=玩家控制：
HousesPrepareHouses=标准阵营
HousesAddHouse=新建阵营
HousesDeleteHouse=删除阵营

; loading dialog
LoadLoadRules=正在加载规则
LoadLoadAI=正在加载 AI 设置
LoadLoadArt=正在加载美术
LoadLoadEva=正在加载语音
LoadLoadTheme=正在加载主题音乐
LoadLoadTutorial=正在加载 tutorial.ini
LoadLoadSound=正在加载音效
LoadLoadSnow=正在加载 Temperat.ini
LoadLoadTemperat=正在加载 Snow.ini
LoadLoadUrban=正在加载 Urban.ini
LoadLoading=正在加载
LoadBuiltBy=制作者：
LoadVersion=版本：
LoadInitDDraw=正在初始化 Direct Draw 6
LoadInitPics=正在加载图形
LoadExtractStdMixFiles=正在打开标准 MIX 文件（可能需要一些时间）

; iso view
IsoCaption=地图视图

; iso view status bar
StructStatus=建筑：
InfStatus=步兵：
AirStatus=飞行器：
UnitStatus=载具：
OvrlStatus=覆盖层：
OvrlDataStatus=覆盖层数据：
CellTagStatus=单元格标签：
TilePlaceStatus=Ctrl：填充模式，Shift：连续绘制，Ctrl+Shift：不对 LAT 或海岸自动平滑
CopyHelp=请点击起点和终点，指定要复制的区域

; iso view object/unit list
NothingObList=无
GroundObList=地面
GroundClearObList=空地
GroundSandObList=地面 1
GroundRoughObList=地面 2
GroundGreenObList=地面 3
GroundPaveObList=路面
GroundWaterObList=水面
NewTunnelObList=创建隧道（双向）
ModifyTunnelObList=修改隧道（双向）
NewTunnelSingleObList=创建隧道（单向）
ModifyTunnelSingleObList=修改隧道（单向）
DelTunnelObList=删除隧道
TunnelObList=隧道
InfantryObList=步兵
VehiclesObList=载具
AircraftObList=飞行器
StructuresObList=建筑
TerrainObList=地形对象
SmudgesObList=污迹
TreesObList=树木
TrafficLightsObList=交通灯
SignsObList=标志
LightPostsObList=灯柱
RndTreeObList=绘制随机树木
OverlayObList=特殊 / 覆盖层
DelOvrlObList=擦除覆盖层
DelOvrl0ObList=擦除单个格子
DelOvrl1ObList=以 1 格半径擦除
DelOvrl2ObList=以 2 格半径擦除
DelOvrl3ObList=以 3 格半径擦除
GrTibObList=绿色泰矿
BlTibObList=蓝色泰矿
DrawRanTibObList=绘制随机泰矿区
DrawTibObList=绘制泰矿
IncTibSizeObList=增大泰矿范围
DecTibSizeObList=缩小泰矿范围
VeinholeObList=脉络洞怪物
VeinsObList=脉络
BridgesObList=桥梁
BigBridgeObList=大桥（空中）
SmallBridgeObList=小桥（地面）
BigTrackBridgeObList=大铁路桥（空中）
SmallConcreteBridgeObList=小混凝土桥
OthObList=其他
AllObList=全部覆盖层
OvrlManuallyObList=手动设置覆盖层（不推荐）
OvrlDataManuallyObList=手动设置覆盖层数据（不推荐）
WaypointsObList=路径点
CreateWaypObList=创建路径点
CreateSpecWaypObList=创建指定 ID 的路径点
DelWaypObList=删除路径点
StartpointsObList=玩家位置
StartpointsPlayerObList=玩家 %1
StartpointsDelete=删除玩家位置
CelltagsObList=单元格标签
CreateCelltagObList=创建单元格标签
DelCelltagObList=删除单元格标签
CelltagPropObList=编辑单元格标签属性
BaseNodesObList=基地节点
CreateNodeNoDelObList=创建节点且不删除建筑
CreateNodeDelObList=创建节点并删除建筑
DelNodeObList=删除节点
DelObjObList=删除对象
ChangeOwnerObList=更改所有者

; celltag dialog
CellTagCap=单元格标签
CellTagDesc=使用单元格标签将指定格子附加到一个标签：
CellTagTag=附加标签：

; aircraft dialog
AirCap=飞行器选项
AirDesc=
AirHouse=所有者：
AirStrength=生命值：
AirState=状态：
AirDirection=方向：
AirTag=附加标签：
AirP1=老兵等级：
AirP2=编组：
AirP3=可招募：
AirP4=AI 可招募：

; structure dialog
StructCap=建筑选项
StructDesc=要为该建筑添加升级，请先设置升级数量，再从升级 1 开始依次设置升级。
StructHouse=所有者：
StructStrength=生命值：
StructDirection=方向：
StructTag=附加标签：
StructP1=可出售：
StructAIRepairs=重建：
StructEnergy=电力支持：
StructUpgradeCount=升级数量：
StructSpotlight=探照灯：
StructUpgrade1=升级 1：
StructUpgrade2=升级 2：
StructUpgrade3=升级 3：
StructP2=AI 维修：
StructP3=显示名称：

; unit dialog
UnitCap=载具
UnitDesc=
UnitHouse=所有者：
UnitStrength=生命值：
UnitState=状态：
UnitDirection=方向：
UnitTag=附加标签：
UnitP1=老兵等级：
UnitP2=编组：
UnitP3=在桥上：
UnitP4=跟随 ID：
UnitP5=可招募：
UnitP6=AI 可招募：

; infantry dialog
InfCap=步兵
InfDesc=
InfHouse=所有者：
InfStrength=生命值：
InfPos=格子位置：
InfState=状态：
InfDirection=方向：
InfTag=附加标签：
InfP1=老兵等级：
InfP2=编组：
InfP3=在桥上：
InfP4=可招募：
InfP5=AI 可招募：

; options dialog
OptCaption=选项
OptExeLabel=泰伯利亚之日可执行文件（请确认路径正确）
OptLanguage=语言：
OptBrowse=浏览
OptSupportGroup=Rules、Art 与 AI 配置文件
OptSupportMods=按泰伯利亚之日方式搜索文件（推荐）
OptOnlyOriginal=仅使用 tibsun.mix 内的文件（不使用火风暴或任何模组）
OptPreferLocalTheater=优先使用 FinalSun 的地形 INI
"""

CHINESE_STRINGS_RA2 = r"""
[Chinese-StringsRA2]
GrTibObList=矿石与宝石
BlTibObList=宝石
DrawRanTibObList=绘制随机矿区
DrawTibObList=绘制矿石
DrawTib2ObList=绘制宝石
IncTibSizeObList=增大范围
DecTibSizeObList=缩小范围
FileRunTiberianSunHelp=启动 红色警戒2
Err_TSNotInstalled=红色警戒2 未正确安装（无法加载 ra2.mix）
GroundSandObListURB=浅色路面
GroundRoughObListURB=泥土
GroundGreenObListURB=草地
GroundPaveObListURB=深色路面
GroundClearObListTEM=浅色草地
GroundRoughObListTEM=深色草地
GroundGreenObListTEM=沙地
GroundClearObListSNO=积雪
GroundSandObListSNO=脏雪
GroundRoughObListSNO=草地
GroundGreenObListSNO=冰面
GroundRoughObListUBN=深色草地
GroundGreenObListUBN=沙地
GroundPave2ObListUBN=城市路面
SAVEDLG_FILETYPES_YR=所有地图|*.mpr%8*.yrm%8*.map%8*.mmx|尤里的复仇多人地图|*.yrm|红色警戒2多人地图|*.mpr%8*.mmx|单人地图|*.map|
SAVEDLG_FILETYPES=红色警戒2地图|*.mpr%8*.map%8*.mmx|红色警戒2多人地图|*.mpr%8*.mmx|红色警戒2单人地图|*.map|
BigTrackBridgeObList=高架木桥
Allied=盟军
Soviet=苏军
Yuri=尤里
Other=其他
BasicTiberiumGrowthEnabled=矿石生长：
SingleplayerFillSilos=填满精炼厂：
OptExeLabel=红色警戒2可执行文件（请确认路径正确）
OptSupportGroup=支持设置
OptSupportMods=支持资料片与模组（推荐）
OptOnlyOriginal=仅支持原版红色警戒2
OptPreferLocalTheater=优先使用 FinalAlert 2 的地形 INI
NewMapCreateDesc=请选择从零创建全新地图，还是导入已有地图（注意：无法导入红色警戒2随机地图生成器生成的地图）
"""

CHINESE_TRANSLATIONS = """
[Chinese-Translations]
; tooltips
Heighten ground (slope logic)=升高地面
Lower ground (slope logic)=降低地面
Make terrain flat=平整地面
Show all tilesets=显示全部图块集
Raise a single tile=升高单个格子（不推荐！）
Lower a single tile=降低单个格子（不推荐！）
Paint cliff front=绘制朝向悬崖
Paint cliff back=绘制背向悬崖
AutoCreate shores=自动创建海岸（仅 FinalAlert 支持）
Autocreate shores=自动创建海岸（AutoShore）
AutoLevel terrain height using cliffs=按悬崖自动调整地面高度（AutoLevel）
Auto level using cliffs=按悬崖自动调整地面高度（AutoLevel）

; other strings
FinalSun Homepage=FinalSun 主页
FinalSun support forum=FinalSun 支持论坛
""" + "\n".join([
    "Manual=说明书",
    "Undo=撤销",
    "Redo=重做",
    "Copy=复制",
    "Paste=粘贴",
    "Disable AutoShore=禁用自动海岸",
    "Disable AutoLat=禁用自动 LAT",
]) + r"""
Event=事件
Action=动作
Events=事件
Actions=动作
Trigger=触发器
Trigger options=触发器选项
Edit=编辑
Map=地图
Basic=常规
Special flags=特殊标志
Lighting=光照
Singleplayer settings=单人设置
Houses=阵营
Trigger editor=触发器编辑器
Tag editor=标签编辑器
Tags (for experts)=标签（高级）
Old trigger editor (obsolete)=旧触发器编辑器（已过时）
Scripts=脚本
Taskforces=特遣部队
Teams=小队
AI Triggers=AI 触发器
AI Trigger enabling=启用 AI 触发器
INI editing=INI 编辑
Terrain=地形
Raise ground=升高地面
Lower ground=降低地面
Flatten ground=平整地面
Hide tileset=隐藏图块集
Show every tileset=显示全部图块集
Hide single field=隐藏单个格子
Show all fields=显示全部格子
Raise single tile (Be careful!)=升高单个图块（小心！）
Lower single tile (Be careful!)=降低单个图块（小心！）
Map tools=地图工具
Change map height=更改地图高度
Ready=就绪
None=无
Aaargh=啊啊啊啊！
File=文件
New=新建
Quit=退出
Open=打开
Save=保存
Save as=另存为
Check map=检查地图
Import mod=导入模组
Run Tiberian Sun=启动泰伯利亚之日
Launch FinalSun version=启动 %9 版本
Copy whole map=复制整张地图
Paste centered=居中粘贴
Local variables (Locals)=局部变量（Locals）
Search Waypoint=查找路径点
Navigate to coordinate=定位到坐标
Tool Scripts=工具脚本
Clear Rock A=空地岩石 A
Clear Rock B=空地岩石 B
Clear Rock C=空地岩石 C
Clear Rock D=空地岩石 D
Clear Rock E=空地岩石 E
Concrete Low Bridge=混凝土低桥
Palette=调色板
Pavement Cliff Box=路面悬崖方块
Sand Rock A=沙地岩石 A
Sand Rock B=沙地岩石 B
Sand Rock C=沙地岩石 C
Sand Rock D=沙地岩石 D
Sand Rock E=沙地岩石 E
Soviet Fortress Wall=苏军要塞墙
Water Crate=水上补给箱
Generic Sandbags=通用沙袋
Generic Black Fence=通用黑色栅栏
Generic Prison Fence=通用监狱围栏
Generic White Fence=通用白色栅栏
Generic Oil Pipe=通用输油管
Generic Barbed Wire=通用铁丝网
Generic Concrete Wall=通用混凝土墙
Generic Brick Fence=通用砖墙
Generic Stone Wall=通用石墙
Black fence=黑色栅栏
Prison camp fence=战俘营围栏
White fence=白色栅栏
Ore=矿石
Small bridge   at the ground=地面小桥
Big bridge   in the air=高架大桥
High wood bridge=高架木桥
Small concrete bridge=小型混凝土桥
Crate=补给箱
Drum=油桶
Goodie Crate=奖励箱
Large Rock A=大型岩石 A
Large Rock B=大型岩石 B
Large Rock C=大型岩石 C
Large Rock D=大型岩石 D
Large Rock E=大型岩石 E
Large Rock F=大型岩石 F
Allied Wall=盟军围墙
Allied Concrete Wall=盟军混凝土墙
Block Base=阻挡建造
Block Base and Movement=阻挡建造与通行
Bridge Bottom-Left to Top-Right=桥梁（左下至右上）
Bridge Top-Left to Bottom-Right=桥梁（左上至右下）
Concrete Low Bridge End 1=混凝土低桥桥头 1
Concrete Low Bridge End 2=混凝土低桥桥头 2
Concrete Low Bridge End 3=混凝土低桥桥头 3
Concrete Low Bridge End 4=混凝土低桥桥头 4
Yuri Wall=尤里围墙
Epsilon Citadel Wall=厄普西隆城堡墙
Foehn Bastion Wall=焚风堡垒墙
Kremlin Wall=克里姆林宫围墙
Kremlin Walls=克里姆林宫围墙
Low Bridge=低桥
A Black Tile A=黑色图块 A
A Black Tile B=黑色图块 B
Destructible Rocks Base=可摧毁岩石基底
Soviet Wall=苏军围墙
Wood Bridge Bottom-Left to Top-Right=木桥（左下至右上）
Wood Bridge Top-Left to Bottom-Right=木桥（左上至右下）
Options=选项
Settings=设置
Show map view=显示地图视图
Show minimap=显示小地图
Easy mode=新手模式
Sounds=音效
Show Building Outline=显示建筑轮廓
Disable Slope Correction=禁用坡度校正
Smooth zoom=平滑缩放
Use default mouse cursor=使用默认鼠标指针
Help=帮助
Info=信息
Show logs=显示日志
GDI Wall=GDI 围墙
Nod Wall=Nod 围墙
Tracks=铁轨
Other=其他
Mobile Construction Vehicle=基地车
Amphibious APC=两栖装甲运兵车
Titan=泰坦
School Bus=校车
Artillery=火炮
Wolverine=狼獾
Hover MLRS=悬浮多管火箭
Locomotive=火车头
Harvester=采矿车
Mammoth Tank=猛犸坦克
Devil's Tongue=魔鬼之舌
Light Infantry=轻步兵
Engineer=工程师
Civilian=平民
Cyborg Commando=生化突击队
Technician=技术员
Orca Fighter=逆戟鲸战斗机
GDI Power Plant=GDI 发电厂
Tiberium Refinery=泰矿精炼厂
Construction Yard=建造场
Barracks=兵营
Sandbags=沙袋
Gate=闸门
Power Turbine=发电涡轮
Pavement=路面
Temple of Nod=诺德神殿
Obelisk of Light=光棱塔
Missile Silo=导弹井
Bridge repair hut=修桥小屋
Red Light Post=红灯柱
Green Light Post=绿灯柱
Blue Light Post=蓝灯柱
Yellow Light Post=黄灯柱
Purple Light Post=紫灯柱
Orange Light Post=橙灯柱
Invisible Red Light Post=隐形红灯柱
Invisible Green Light Post=隐形绿灯柱
Invisible Blue Light Post=隐形蓝灯柱
Invisible Yellow Light Post=隐形黄灯柱
Invisible Purple Light Post=隐形紫灯柱
Invisible Orange Light Post=隐形橙灯柱
Invisible Light Post=隐形灯柱
Light Post=灯柱
Light Tower=灯塔
Dam=水坝
Church=教堂
Pyramid=金字塔
Scrin Ship=斯克林飞船
Tree=树木
Tiberium Tree=泰矿树
Boxes=箱子
Bridge 1=桥梁（左上-右下）
Bridge 2=桥梁（左下-右上）
Railroad Bridge 1=铁路桥（左上-右下）
Railroad Bridge 2=铁路桥（左下-右上）
Wood Bridge 1=木桥（左上-右下）
Wood Bridge 2=木桥（左下-右上）

; FA2:YR
FinalAlert 2 Fansite link=%9 粉丝站链接
FinalAlert 2 Forum=%9 论坛
"""

CHINESE_TRANSLATIONS_RA2 = r"""
[Chinese-TranslationsRA2]
Tiberium=矿石
Gems=宝石
Run Tiberian Sun=启动红色警戒2
Tiberium Tree=矿石矿
Countries=阵营
FinalSun Homepage=FinalAlert 主页
FinalSun support forum=FinalAlert 支持论坛
AutoCreate shores=自动创建海岸（AutoShore）
Tiberium (Blue)=矿石
Tiberium (Green)=矿石
"""

ENGLISH_EXTRA_STRINGS = r"""
Next=Next
Browse=Browse
OptCaption=Options
OptExeLabel=Tiberian Sun EXE (make sure its in the correct path)
OptLanguage=Language:
OptBrowse=Browse
OptSupportGroup=Rules, Art && AI ini files
OptSupportMods=Act like Tiberian Sun when searching for files (recommended)
OptOnlyOriginal=Only use the files inside tibsun.mix (do not use Firestorm or any mods)
OptPreferLocalTheater=Prefer FinalSun theater INI files
NewMapStep1Cap=Create new map - Step 1
NewMapStep2Cap=Create new map - Step 2
NewMapStep3Cap=Create new map - Step 3
NewMapStep4Cap=Create new map - Step 4
NewMapTypeDesc=Please select if you want to create a new singleplayer map or multiplayer map. Multiplayer maps are maps used in both skirmish and multiplayer games.
NewMapTypeSingle=Singleplayer map (only for experienced users that can handle advanced mode)
NewMapTypeMulti=Multiplayer map
NewMapCreateDesc=Please select here if you want to create a completely new map from scratch or if you want to import an already existing map (Note: You cannot import maps created by the random map generator of RA2 or of TS version 1.15 or higher)
NewMapCreateNew=Create a completely new map
NewMapCreateImport=Import an existing map or bitmap (BMP)
NewMapCreateAITriggers=Activate AI Triggers (results in better AI)
NewMapSpDesc=Please select the human player. It is recommended to leave the other options as they are.
NewMapCreateNewDesc=Please select size, theater and starting height here. The size should be below 100x100 for best results.
NewMapWidth=Width:
NewMapHeight=Height:
NewMapTheater=Theater:
NewMapStartingHeight=Starting height:
NewMapImportDesc=Please select the map you want to import. No triggers or houses will be imported. Bitmaps will be scaled down to appropiate size. Currently bitmaps will always be converted to temperate theater maps.
"""

ENGLISH_EXTRA_STRINGS_RA2 = r"""
OptExeLabel=Red Alert 2 EXE (make sure its in the correct path)
OptSupportGroup=Support settings
OptSupportMods=Support mission disks and mods (recommended)
OptOnlyOriginal=Only support original Red Alert 2
OptPreferLocalTheater=Prefer FinalAlert 2 theater INI files
NewMapCreateDesc=Please select here if you want to create a completely new map from scratch or if you want to import an already existing map (Note: You cannot import maps created by the random map generator of RA2)
"""

GERMAN_EXTRA_STRINGS = r"""
Next=Weiter
Browse=Durchsuchen
OptCaption=Optionen
OptExeLabel=Tiberian Sun EXE (bitte den korrekten Pfad angeben)
OptLanguage=Sprache:
OptBrowse=Durchsuchen
OptSupportGroup=Rules, Art && AI INI-Dateien
OptSupportMods=Dateien wie Tiberian Sun suchen (empfohlen)
OptOnlyOriginal=Nur Dateien aus tibsun.mix verwenden (kein Firestorm und keine Mods)
OptPreferLocalTheater=FinalSun Theater-INI-Dateien bevorzugen
NewMapStep1Cap=Neue Karte - Schritt 1
NewMapStep2Cap=Neue Karte - Schritt 2
NewMapStep3Cap=Neue Karte - Schritt 3
NewMapStep4Cap=Neue Karte - Schritt 4
NewMapTypeDesc=Bitte wählen Sie, ob Sie eine Einzelspieler- oder Mehrspielerkarte erstellen möchten. Mehrspielerkarten werden in Gefecht und Mehrspieler verwendet.
NewMapTypeSingle=Einzelspielerkarte (nur für erfahrene Benutzer im Profimodus)
NewMapTypeMulti=Mehrspielerkarte
NewMapCreateDesc=Bitte wählen Sie, ob Sie eine komplett neue Karte erstellen oder eine bestehende Karte importieren möchten (Hinweis: Karten des Zufallsgenerators von RA2 oder TS 1.15+ können nicht importiert werden)
NewMapCreateNew=Komplett neue Karte erstellen
NewMapCreateImport=Bestehende Karte oder Bitmap (BMP) importieren
NewMapCreateAITriggers=KI-Auslöser aktivieren (bessere KI)
NewMapSpDesc=Bitte wählen Sie den menschlichen Spieler. Die übrigen Optionen sollten Sie belassen.
NewMapCreateNewDesc=Bitte Größe, Theater und Starthöhe wählen. Für beste Ergebnisse sollte die Größe unter 100x100 liegen.
NewMapWidth=Breite:
NewMapHeight=Höhe:
NewMapTheater=Theater:
NewMapStartingHeight=Starthöhe:
NewMapImportDesc=Bitte wählen Sie die zu importierende Karte. Auslöser und Häuser werden nicht importiert. Bitmaps werden verkleinert. Bitmaps werden derzeit immer in gemäßigte Theaterkarten umgewandelt.
"""

GERMAN_EXTRA_STRINGS_RA2 = r"""
OptExeLabel=Alarmstufe Rot 2 EXE (bitte den korrekten Pfad angeben)
OptSupportGroup=Unterstützungseinstellungen
OptSupportMods=Missionsdisks und Mods unterstützen (empfohlen)
OptOnlyOriginal=Nur das originale Alarmstufe Rot 2 unterstützen
OptPreferLocalTheater=FinalAlert 2 Theater-INI-Dateien bevorzugen
NewMapCreateDesc=Bitte wählen Sie, ob Sie eine komplett neue Karte erstellen oder eine bestehende Karte importieren möchten (Hinweis: Karten des Zufallsgenerators von RA2 können nicht importiert werden)
"""

FS_STRINGS_OVERRIDE = {
    "StrChangeHeight": "请输入要应用到每个格子的高度差。允许使用负值！",
    "StrChangeHeightErr": "错误，这会把地图弄乱！",
    "HousesDesc": "阵营代表不同的玩家，包括 AI 和人类。如果这是多人地图，请不要在这里创建阵营！如果这是单人地图且还没有任何阵营，请先点击“标准阵营”，再按需创建额外阵营，然后选择人类玩家（别忘了为该阵营启用“玩家控制”）！",
    "GrTibObList": "泰矿",
    "DrawTibObList": "绘制绿色泰矿",
    "DrawTib2ObList": "绘制蓝色泰矿",
}


def insert_after(text: str, marker: str, insertion: str) -> str:
    idx = text.find(marker)
    if idx < 0:
        raise SystemExit(f"marker not found: {marker!r}")
    idx += len(marker)
    return text[:idx] + insertion + text[idx:]


def insert_before_section_end(text: str, section: str, extra: str) -> str:
    marker = section if section.endswith("\n") else section + "\n"
    start = text.find(marker)
    if start < 0:
        raise SystemExit(f"section not found: {section}")
    nxt = text.find("\n[", start + len(marker))
    extra = extra.strip("\n") + "\n"
    if extra in text[start:nxt if nxt >= 0 else None]:
        return text
    if nxt < 0:
        if not text.endswith("\n"):
            text += "\n"
        return text + extra
    return text[:nxt] + "\n" + extra + text[nxt:]


def patch_language(path: Path, languages_block: str, is_finalsun: bool) -> None:
    raw = path.read_bytes()
    bom = raw.startswith(b"\xef\xbb\xbf")
    text = raw.decode("utf-8-sig").replace("\r\n", "\n").replace("\r", "\n")

    if "2=Chinese" in text or "4=Chinese" in text:
        print(f"skip already patched languages: {path}")
    else:
        if "[Languages]\n0=English\n1=German\n\n" in text and not is_finalsun:
            text = text.replace(
                "[Languages]\n0=English\n1=German\n",
                languages_block,
                1,
            )
        elif "[Languages]\n0=English\n1=German\n2=Swedish\n3=Nederlands\n" in text and is_finalsun:
            text = text.replace(
                "[Languages]\n0=English\n1=German\n2=Swedish\n3=Nederlands\n",
                languages_block,
                1,
            )
        else:
            raise SystemExit(f"unexpected [Languages] block in {path}")

    if "[ChineseHeader]" not in text:
        text = insert_after(
            text,
            "[NederlandsHeader]\nName=Nederlands\nVersion=1\nExtensionName=NL\n",
            CHINESE_HEADER,
        )

    text = insert_before_section_end(text, "[English-Strings]", ENGLISH_EXTRA_STRINGS)
    text = insert_before_section_end(text, "[English-StringsRA2]", ENGLISH_EXTRA_STRINGS_RA2)
    text = insert_before_section_end(text, "[German-Strings]", GERMAN_EXTRA_STRINGS)
    if "[German-StringsRA2]" in text:
        text = insert_before_section_end(text, "[German-StringsRA2]", GERMAN_EXTRA_STRINGS_RA2)

    if "[Chinese-Strings]" not in text:
        strings = CHINESE_STRINGS
        strings_ra2 = CHINESE_STRINGS_RA2
        if is_finalsun:
            for key, value in FS_STRINGS_OVERRIDE.items():
                strings = re.sub(rf"^{re.escape(key)}=.*$", f"{key}={value}", strings, count=1, flags=re.M)
        text = text.rstrip() + "\n\n" + strings_ra2.strip() + "\n\n" + strings.strip() + "\n\n"
        text += CHINESE_TRANSLATIONS_RA2.strip() + "\n\n" + CHINESE_TRANSLATIONS.strip() + "\n"

    data = text.replace("\n", "\r\n").encode("utf-8")
    if bom:
        data = b"\xef\xbb\xbf" + data
    path.write_bytes(data)
    print(f"updated {path}")


def patch_rc(path: Path) -> None:
    raw = path.read_bytes()
    newline = "\r\n" if b"\r\n" in raw else "\n"
    text = raw.decode("utf-8").replace("\r\n", "\n").replace("\r", "\n")
    text2 = re.sub(r'FONT 8, "MS Sans Serif"(, 0, 0, 0x[01])?', 'FONT 8, "MS Shell Dlg 2", 0, 0, 1', text)
    text2 = re.sub(r'FONT 8, "Tahoma"(, 0, 0, 0x[01])?', 'FONT 8, "MS Shell Dlg 2", 0, 0, 1', text2)
    if text2 == text:
        print("rc fonts already updated")
    else:
        path.write_bytes(text2.replace("\n", newline).encode("utf-8"))
        print(f"updated fonts in {path}")


def main() -> None:
    patch_language(
        ROOT / "MissionEditor" / "data" / "FinalAlert2" / "FALanguage.ini",
        "[Languages]\n0=English\n1=German\n2=Chinese\n",
        is_finalsun=False,
    )
    patch_language(
        ROOT / "MissionEditor" / "data" / "FinalSun" / "FSLanguage.ini",
        "[Languages]\n0=English\n1=German\n2=Swedish\n3=Nederlands\n4=Chinese\n",
        is_finalsun=True,
    )
    patch_rc(ROOT / "MissionEditor" / "MissionEditor.rc")


if __name__ == "__main__":
    main()
