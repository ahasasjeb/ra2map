# -*- coding: utf-8 -*-
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

EN = r"""
MapLoadCap=Loading
MapLoadDesc=Loading map, please wait. This may take several minutes on big maps.
SavingCap=Saving
SavingDesc=Please wait a few seconds while %9 is saving. On very large maps this may even take some minutes, please do not cancel! More information is available in the status bar, most time will be needed for packing.
ShutdownCap=Shutdown
ShutdownDesc=Shutting down, please wait a few seconds
LoadGraphicsWait=Loading graphics, please wait a few seconds.
ProgressCap=Progress
ProgressPrefix=Progress:
UpdatingObjects=Updating objects, please wait
UpdatingMinimap=Updating Minimap, please wait
PackingData=Packing data...
SavingStatus=Saving...
SavingStatusPct=Saving... %1 ( %2% )
LoadingGraphics=Loading graphics
TerrainHeightWait=Applying non-morphable terrain height change, please wait...
MapCorrupt=This map seems to be corrupt. Do you want to try repairing it? If you click cancel, an empty map will be created. If you click no, it will load the map as it is.
MapCorruptCap=Corrupt
TheaterDisabled=You have selected to don't show temperate or snow theater, but this map uses this theater. You cannot load it without restarting %9 with this theater enabled.
FileReadOnly=Error: file cannot be saved. Make sure the file is not read only
ExportRulesIni=This will export the Rules.Ini of Tiberian Sun V1.13 MMX. You should not modify this rules.ini because you won't be able to play online then and because this could cause compatibility problems.\nIf you want to modify the rules.ini, you need to rename it before you play online.
ExportRulesIniCap=Export Rules.INI
ExportRulesHelp=Export the file rules.ini
AutoLevelerDesc=This tool will try to automatically raise the terrain using the cliffs.\nIt may take some seconds to execute, as there are masses of data to handle.\nAfter this, you should check your map if everything looks fine. If not, you should use the different height tools, especially flatten ground, to fix any errors. You can use Edit->Undo to undo anything that has been done by using this function.
AutoLevelerCap=Auto Leveler
NoCliffsLunar=There are no cliffs in the Lunar theater
TunnelsSizeWarning=Tunnels may be damaged after changing the map size. Continue?
Warning=Warning
MapSizeRA2Warn=Width + height is bigger than 256, this may cause problems in RA2. Continue?
MapSizeTSWarn=Width + height is bigger than 256, this may cause problems in TS. Continue?
CannotPlaceNode=You cannot place a node on another node
Fill1x1Only=You can only use 1x1 tiles to fill areas.
StackTooSmall=Stack is too small to complete operation!
NoTagsSpecified=No tags are specified.
LanguageFileMissing=String file not found, using rules.ini names
HousesAlreadyExist=There are already houses in your map. You need to delete these first.
HouseNameTaken=Sorry this name is not available. %1 is already used in the map file. You need to use another name.
NoHousesExist=No houses do exist, if you want to use houses, you should use Prepare houses before doing anything else.
NoHousesExistMP=No houses do exist, if you want to use houses, you should use Prepare houses before doing anything else. Note that in a multiplayer map independent computer players cannot be created by using the names GDI and Nod for the house. Just use something like GDI_AI.
IniNeedSection=You need to specify a section first.
IniNeedChooseSection=You cannot delete a section without choosing one.
IniNeedChooseKey=You cannot delete a key without choosing one.
IniConfirmDelete=Are you sure you want to delete %1? You should be really careful, you may not be able to use the map afterwards.
IniDeleteSectionCap=Delete section
IniDeleteKeyCap=Delete key
IniNoContent=File does not have any ini content, abort.
DeleteScriptType=Are you sure to delete this ScriptType? Don't forget to delete any references to this ScriptType
DeleteScriptTypeCap=Delete ScriptType
DeleteTag=Are you sure to delete the selected tag? This may cause the attached trigger to don't work anymore, if no other tag has the trigger attached.
DeleteTagCap=Delete tag
NeedTriggerForTag=Before creating tags, you need at least one trigger.
DeleteTaskForce=Are you sure to delete the selected task force? If you delete it, make sure to eliminate ANY references to this task force in team-types.
DeleteTaskForceCap=Delete task force
DeleteTeamType=Are you sure that you want to delete the selected team-type? If you delete it, don't forget to delete any reference to the team-type.
DeleteTeamTypeCap=Delete team-type
DeleteAction=Do you really want to delete this action?
DeleteActionCap=Delete action
DeleteEvent=Do you really want to delete this event?
DeleteEventCap=Delete event
DeleteTriggerAsk=Do you really want to delete this trigger? Don't forget to delete the attached tag (important!)
DeleteTriggerCap=Delete trigger
DeleteTriggerTags=If you want to delete all attached tags, too, press Yes.\nIf you don't want to delete these tags, press No.\nIf you want to cancel deletion of the trigger, press Cancel.\n\nNote: CellTags will never be deleted using this function
TriggerCreated=Trigger created. If you want to create a simple tag now, press Yes. The tag will be called New tag, you should name it like the trigger (after you have set up the trigger).
TriggerCreatedCap=Trigger created
SelectCurrentTrigger=Select current trigger:
NewTrigger=New trigger
DeleteTriggerBtn=Delete trigger
PlaceOnMap=Place on map
CloneTrigger=Clone trigger
SearchBtn=Search
WaypointsLabel=Waypoints:
SpecTibGrows=Tiberium grows:
SpecTibSpreads=Tiberium spreads:
SpecTibExplosive=Tiberium explosive:
SpecDestroyableBridges=Destroyable bridges:
SpecMCVDeploy=MCV deploy:
SpecInitialVeteran=Initial veteran: (initial troops have veteran status)
SpecFixedAlliance=Fixed alliance:
SpecHarvesterImmune=Harvester immune:
SpecFogOfWar=Fog of war:
SpecInert=Inert:
SpecIonStorms=Ion storms:
SpecMeteorites=Meteorites:
SpecVisceroids=Visceroids:
SpecDesc=Some settings only work under certain circumstances.
SavePreviewNew=Create new preview using minimap
SavePreviewExisting=Use existing preview (if possible)
SavePreviewNone=Don't save any preview
SaveMapName=Map name:
SavePreviewGroup=Preview
"""

EN_RA2 = r"""
SpecTibGrows=Ore grows:
SpecTibSpreads=Ore spreads:
"""

ZH = r"""
MapLoadCap=正在加载
MapLoadDesc=正在加载地图，请稍候。大地图可能需要几分钟。
SavingCap=正在保存
SavingDesc=正在保存，请稍候。超大地图甚至可能需要几分钟，请不要取消！更多信息见状态栏，大部分时间会花在打包上。
ShutdownCap=正在关闭
ShutdownDesc=正在关闭，请稍候几秒
LoadGraphicsWait=正在加载图形，请稍候几秒。
ProgressCap=进度
ProgressPrefix=进度：
UpdatingObjects=正在更新对象，请稍候
UpdatingMinimap=正在更新小地图，请稍候
PackingData=正在打包数据...
SavingStatus=正在保存...
SavingStatusPct=正在保存... %1（%2%）
LoadingGraphics=正在加载图形
TerrainHeightWait=正在应用不可变形地形高度变化，请稍候...
MapCorrupt=这张地图似乎已损坏。是否尝试修复？点击“取消”将创建空白地图，点击“否”将按原样加载。
MapCorruptCap=地图损坏
TheaterDisabled=你关闭了温带或雪地地形的显示，但这张地图使用了该地形。必须重新启动 %9 并启用该地形后才能加载。
FileReadOnly=错误：无法保存文件。请确认文件不是只读。
ExportRulesIni=这将导出泰伯利亚之日 V1.13 MMX 的 Rules.Ini。你不应直接修改此 rules.ini，否则将无法联机，并可能导致兼容性问题。\n若要修改 rules.ini，请先重命名，再进行联机游戏。
ExportRulesIniCap=导出 Rules.INI
ExportRulesHelp=导出 rules.ini 文件
AutoLevelerDesc=此工具会尝试利用悬崖自动抬升地形。\n处理数据量很大，可能需要数秒。\n完成后请检查地图是否正常。如有问题，请使用各种高度工具（尤其是平整地面）进行修复。可用 编辑->撤销 撤销本次操作。
AutoLevelerCap=自动平整
NoCliffsLunar=月球地形中没有悬崖
TunnelsSizeWarning=更改地图尺寸后隧道可能损坏。是否继续？
Warning=警告
MapSizeRA2Warn=宽度与高度之和大于 256，红色警戒2 中可能出现问题。是否继续？
MapSizeTSWarn=宽度与高度之和大于 256，泰伯利亚之日中可能出现问题。是否继续？
CannotPlaceNode=不能把节点放在另一个节点上
Fill1x1Only=只能使用 1x1 图块填充区域。
StackTooSmall=堆栈太小，无法完成操作！
NoTagsSpecified=尚未指定任何标签。
LanguageFileMissing=找不到字符串文件，将使用 rules.ini 中的名称
HousesAlreadyExist=地图中已有阵营。请先删除现有阵营。
HouseNameTaken=该名称不可用。%1 已在地图文件中使用，请换一个名称。
NoHousesExist=当前没有任何阵营。若要使用阵营，请先点击“标准阵营”。
NoHousesExistMP=当前没有任何阵营。若要使用阵营，请先点击“标准阵营”。注意：多人地图中不能用 GDI 和 Nod 作为独立电脑玩家的阵营名，请使用类似 GDI_AI 的名称。
IniNeedSection=必须先指定一个区段。
IniNeedChooseSection=删除区段前必须先选择一个区段。
IniNeedChooseKey=删除键前必须先选择一个键。
IniConfirmDelete=确定要删除 %1 吗？请务必小心，否则之后可能无法再使用该地图。
IniDeleteSectionCap=删除区段
IniDeleteKeyCap=删除键
IniNoContent=文件不包含任何 INI 内容，已中止。
DeleteScriptType=确定要删除此脚本类型吗？别忘了删除所有对它的引用。
DeleteScriptTypeCap=删除脚本类型
DeleteTag=确定要删除所选标签吗？如果没有其他标签引用该触发器，附加的触发器可能无法工作。
DeleteTagCap=删除标签
NeedTriggerForTag=创建标签之前，至少需要一个触发器。
DeleteTaskForce=确定要删除所选特遣部队吗？删除后请务必清除小队类型中所有对它的引用。
DeleteTaskForceCap=删除特遣部队
DeleteTeamType=确定要删除所选小队类型吗？删除后请务必清除所有对它的引用。
DeleteTeamTypeCap=删除小队类型
DeleteAction=确定要删除此动作吗？
DeleteActionCap=删除动作
DeleteEvent=确定要删除此事件吗？
DeleteEventCap=删除事件
DeleteTriggerAsk=确定要删除此触发器吗？别忘了删除附加的标签（很重要！）
DeleteTriggerCap=删除触发器
DeleteTriggerTags=如果还要删除所有附加标签，请点“是”。\n如果不删除这些标签，请点“否”。\n如果要取消删除触发器，请点“取消”。\n\n注意：此功能不会删除单元格标签。
TriggerCreated=触发器已创建。如果现在要创建一个简单标签，请点“是”。标签将命名为 New tag，设置完触发器后建议改成与触发器相同的名称。
TriggerCreatedCap=触发器已创建
SelectCurrentTrigger=选择当前触发器：
NewTrigger=新建触发器
DeleteTriggerBtn=删除触发器
PlaceOnMap=放到地图上
CloneTrigger=克隆触发器
SearchBtn=查找
WaypointsLabel=路径点：
SpecTibGrows=泰矿生长：
SpecTibSpreads=泰矿扩散：
SpecTibExplosive=泰矿爆炸：
SpecDestroyableBridges=可摧毁桥梁：
SpecMCVDeploy=基地车展开：
SpecInitialVeteran=初始老兵：（开局部队具有老兵等级）
SpecFixedAlliance=固定同盟：
SpecHarvesterImmune=采矿车免疫：
SpecFogOfWar=战争迷雾：
SpecInert=惰性：
SpecIonStorms=离子风暴：
SpecMeteorites=陨石：
SpecVisceroids=内脏怪：
SpecDesc=部分设置仅在特定情况下生效。
SavePreviewNew=使用小地图创建新预览
SavePreviewExisting=使用已有预览（如可能）
SavePreviewNone=不保存预览
SaveMapName=地图名称：
SavePreviewGroup=预览
"""

ZH_RA2 = r"""
SpecTibGrows=矿石生长：
SpecTibSpreads=矿石扩散：
"""

ZH_TR = r"""
Search Waypoint=查找路径点
Change Map Size=更改地图尺寸
Save options=保存选项
INI Editor=INI 编辑器
Lighting Settings=光照设置
Special Flags=特殊标志
Local Variables=局部变量
Map Scripts=地图脚本
Random Tree Placing=随机放置树木
Create waypoint=创建路径点
Search=查找
Warning=警告
Corrupt=地图损坏
Progress=进度
Loading=正在加载
Saving=正在保存
Shutdown=正在关闭
Specify file type=指定文件类型
MMX options=MMX 选项
"""


def insert_before_section_end(text: str, section: str, extra: str) -> str:
    marker = section + "\n"
    start = text.find(marker)
    if start < 0:
        raise SystemExit(f"section not found: {section}")
    nxt = text.find("\n[", start + len(marker))
    extra = extra.strip("\n") + "\n"
    probe = extra.splitlines()[0]
    chunk = text[start:nxt if nxt >= 0 else None]
    if probe in chunk:
        return text
    if nxt < 0:
        if not text.endswith("\n"):
            text += "\n"
        return text + extra
    return text[:nxt] + "\n" + extra + text[nxt:]


def patch(path: Path) -> None:
    raw = path.read_bytes()
    bom = raw.startswith(b"\xef\xbb\xbf")
    text = raw.decode("utf-8-sig").replace("\r\n", "\n").replace("\r", "\n")
    text = insert_before_section_end(text, "[English-Strings]", EN)
    text = insert_before_section_end(text, "[English-StringsRA2]", EN_RA2)
    text = insert_before_section_end(text, "[Chinese-Strings]", ZH)
    text = insert_before_section_end(text, "[Chinese-StringsRA2]", ZH_RA2)
    text = insert_before_section_end(text, "[Chinese-Translations]", ZH_TR)
    data = text.replace("\n", "\r\n").encode("utf-8")
    if bom:
        data = b"\xef\xbb\xbf" + data
    path.write_bytes(data)
    print("updated", path)


def main() -> None:
    patch(ROOT / "MissionEditor" / "data" / "FinalAlert2" / "FALanguage.ini")
    patch(ROOT / "MissionEditor" / "data" / "FinalSun" / "FSLanguage.ini")


if __name__ == "__main__":
    main()
