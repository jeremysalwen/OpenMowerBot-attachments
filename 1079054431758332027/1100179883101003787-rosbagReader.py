import ctypes
import uuid
from matplotlib.axes import Axes

import rosbag
from geometry_msgs.msg  import Point32

import numpy as np
from matplotlib.lines import Line2D

import matplotlib.pyplot as plt
from enum import Enum

import matplotlib.pyplot as plt
from matplotlib.patches import Patch, Polygon as Poly

from geometry_msgs.msg import Point32
from geometry_msgs.msg import Polygon

class AreaType(Enum):
    MOWING_AREA = 1
    NAVIGATION_AREA = 2
    RESTRICTED_AREA = 3
    DOCKING_POINT = 4

class InteractorEvent(Enum):
    MOWING_AREA_ADDED = 1
    MOWING_AREA_DELETED = 2
    NAVIGATION_AREA_DELETED = 3
    RESTRICTED_AREA_ADDED = 4
    RESTRICTED_AREA_DELETED = 5
    DOCKING_POINT_DELETED = 6

class MessageBoxStyle(Enum):
    OK = 0
    OKCancel = 1
    AbortRetryIgnore = 2
    YesNoCancel = 3
    YesNo = 4
    RetryCancel = 5
    CancelTryAgainContinue = 6
    ##  0 : OK
##  1 : OK | Cancel
##  2 : Abort | Retry | Ignore
##  3 : Yes | No | Cancel
##  4 : Yes | No
##  5 : Retry | Cancel 
##  6 : Cancel | Try Again | Continue

class AxesText(Enum):
    PLOT_TITLE = 'Click, drag or use shortcut keys'
    DEFAULT = 'd delete point or area\ni insert point or area\nh hide area\nm merge mowing areas\nc convert area to restricted area\n[digit]+click mowing area to set mowing order\n\n\n\n\n'
    POINT_SELECTED = 'd delete point or area\ni insert point or area\nh hide area\nm merge mowing areas\nc convert area to restricted area\n[digit]+click mowing area to set mowing order\n\u2190 move the point to the left by 10cm (shift+\u2190 by 1cm)\n\u2192 move the point to the right by 10cm (shift+\u2192 by 1cm)\n\u2191 move the point to the top by 10cm (shift+\u2191 by 1cm)\n\u2193 move the point to the bottom by 10cm (shift+\u2193 by 1cm)\n'

class StatusBarText(Enum):
    FIRST = 1
    SECOND = 2
    THIRD = 3
    FOURTH = 4
    FIFTH = 5
    SIXTH = 6
    SEVENTH = 7
    EIGHTH = 8
    NINTH = 9
    MOWING_AREA_ORDER = 'Mowing Area set as '
    MOWING_AREA_TO_MOW = ' to mow'
    SELECT_AREA_TO_MERGE = 'Select Area to merge'
    SELECT_POINT_TO_MERGE = 'Select Point of Area to merge with'
    AREAS_MERGED = 'Areas merged'
    SELECT_AREA_TO_CONVERT = 'Select Area to convert to Restricted Area'
    SELECT_AREA_TO_ADD_RESTRICIONS = 'Select Area to add Restricted Area to'
    AREA_CONVERTED = 'Area converted'

class Status(Enum):
    IDLE = 0,
    MERGE_SELECTING_AREA = 1,
    MERGE_SELECTING_MERGE_POINT = 2
    CONVERT_SELECTING_SOURCE_AREA = 3
    CONVERT_SELECTING_TARGET_AREA = 4

class PolygonInteractor:
    def __init__(self, ax, points: list[Point32], areaType: AreaType, eventHandler = None):
    
        self.uuid = uuid.uuid4()
        self.areaType = areaType
        self.eventHandler = eventHandler
        self.poly = self.getPolygon(points, areaType)
        self.ax = ax
        self.ax.add_patch(self.poly)
        self.line = self.getLine(self.poly.xy, areaType)         
        self.ax.add_line(self.line)
        self.canvas = self.ax.figure.canvas
        self.canvas.zorder += 1
        self.selectedPointIndex = None
        self.poly.zorder = self.canvas.zorder
     
        self.setAxesLimits(self.poly.xy)

        # self.canvas.background = self.canvas.copy_from_bbox(self.ax.bbox)
        self.on_draw_cid = self.canvas.mpl_connect('draw_event', self.on_draw)
        self.redraw()

    def getPolygon(self, points, areaType: AreaType):
        x_list = []
        y_list = []
        for point in points:
            x_list.append(point.x)
            y_list.append(point.y)

        faceclr = 'limegreen'
        match areaType:
            case AreaType.NAVIGATION_AREA:
                faceclr = 'white'
            case AreaType.RESTRICTED_AREA:
                faceclr = 'red'
            case AreaType.DOCKING_POINT:
                faceclr = 'deepskyblue' 

        return Poly(np.column_stack([x_list, y_list]), animated=True, facecolor=faceclr)

    def getLine(self, points, areaType: AreaType):
        markerfaceclr = 'forestgreen'
        match areaType:
            case AreaType.NAVIGATION_AREA:
                markerfaceclr = 'black'
            case AreaType.RESTRICTED_AREA:
                markerfaceclr = 'firebrick'
            case AreaType.DOCKING_POINT:
                markerfaceclr = 'blue' 

        x, y = zip(*points)
        return Line2D(x, y, marker='o', markerfacecolor=markerfaceclr, markersize=6, animated=True)

    def setAxesLimits(self, points):
        x, y = zip(*points)
        x = list(x)
        y = list(y)
        xlim = self.ax.get_xlim()
        ylim = self.ax.get_ylim()
        if xlim[0] != 0:
            x.append(xlim[0])
            x.append(xlim[1])

        self.ax.set_xlim(min(x) - 1, max(x) + 1)

        if ylim[0] != 0:
            y.append(ylim[0])
            y.append(ylim[1])

        self.ax.set_ylim(min(y) - 1, max(y) + 1)

    def selectPoint(self, pointIndex):
        self.selectedPointIndex = pointIndex
        self.line.set_markersize(10)
        self.line.set_markevery(slice(pointIndex, pointIndex + 1, 1))
        self.redraw()

    def deselectPoint(self):
        if self.selectedPointIndex == None:
            return
        self.selectedPointIndex = None
        self.line.set_markersize(6)
        self.line.set_markevery(None)
        self.redraw()

    def movePoint(self, pointIndex, key, x = None, y = None):
        match key:
            case 'left':
                self.poly.xy[pointIndex] = self.poly.xy[pointIndex][0] - 0.1, self.poly.xy[pointIndex][1]
            case 'shift+left':
                self.poly.xy[pointIndex] = self.poly.xy[pointIndex][0] - 0.01, self.poly.xy[pointIndex][1]
            case 'right':
                self.poly.xy[pointIndex] = self.poly.xy[pointIndex][0] + 0.1, self.poly.xy[pointIndex][1]
            case 'shift+right':
                self.poly.xy[pointIndex] = self.poly.xy[pointIndex][0] + 0.01, self.poly.xy[pointIndex][1]
            case 'up':
                self.poly.xy[pointIndex] = self.poly.xy[pointIndex][0], self.poly.xy[pointIndex][1] + 0.1
            case 'shift+up':
                self.poly.xy[pointIndex] = self.poly.xy[pointIndex][0], self.poly.xy[pointIndex][1] + 0.01
            case 'down':
                self.poly.xy[pointIndex] = self.poly.xy[pointIndex][0], self.poly.xy[pointIndex][1] - 0.1
            case 'shift+down':
                self.poly.xy[pointIndex] = self.poly.xy[pointIndex][0], self.poly.xy[pointIndex][1] - 0.01
            case 'mouse+move':
                self.poly.xy[pointIndex] = x, y
                if pointIndex == 0:
                    self.poly.xy[-1] = x, y
                elif pointIndex == len(self.poly.xy) - 1:
                    self.poly.xy[0] = x, y
        
        self.line.set_data(zip(*self.poly.xy))
        self.redraw()

    def addPoints(self, points):
        self.poly.xy = points
        self.line.set_data(zip(*self.poly.xy))
        self.redraw()

    def addPoint(self, index):
        self.poly.xy = np.insert(self.poly.xy, index+1, self.poly.xy[index], axis=0)
        self.line.set_data(zip(*self.poly.xy))
        self.redraw()

    def deletePoint(self, pointIndex):
        self.deselectPoint()
        self.poly.xy = np.delete(self.poly.xy, pointIndex, axis=0)
        self.line.set_data(zip(*self.poly.xy))

        if len(self.poly.xy) < 3:
            self.delete()

        self.redraw()

    def delete(self):
        self.poly.remove()
        self.poly = None
        self.line.remove()
        self.line = None
        self.redraw()
        self.canvas.mpl_disconnect(self.on_draw_cid)
        match self.areaType:
            case AreaType.MOWING_AREA:
                self.eventHandler(InteractorEvent.MOWING_AREA_DELETED)
            case AreaType.NAVIGATION_AREA:
                self.eventHandler(InteractorEvent.NAVIGATION_AREA_DELETED)
            case AreaType.RESTRICTED_AREA:
                self.eventHandler(InteractorEvent.RESTRICTED_AREA_DELETED, self)

    def hide(self):
        self.poly.set_visible(False)
        self.line.set_visible(False)
        self.redraw()

    def on_draw(self, event):
        if self.poly != None:
            self.ax.draw_artist(self.poly)
        if self.line != None:
            self.ax.draw_artist(self.line)
        # do not need to blit here, this will fire before the screen is
        # updated

    def redraw(self):
        self.canvas.draw()
        return

        # self.canvas.background = self.canvas.copy_from_bbox(self.ax.bbox)
        # self.canvas.restore_region(self.canvas.background)
        # self.ax.draw_artist(self.poly)
        # self.ax.draw_artist(self.line)
        # self.canvas.blit(self.ax.bbox)

class DockingPoint:
    def __init__(self, ax: Axes, msg):

        # self._msg = type(msg)()
        self._msg = msg
        self._interactor = PolygonInteractor(ax, [msg.position], AreaType.DOCKING_POINT)

    def getMsg(self):
        if self._interactor == None or self._interactor.poly == None:
            return None
        
        self._msg.position = Point32(self._interactor.poly.xy[0][0], self._interactor.poly.xy[0][1], 0)

        return self._msg

class Area:
    def __init__(self, ax: Axes, msg, areaType: AreaType, eventHandler):
        self._ax = ax
        # self._msg = type(msg)()
        self._msg = msg
        self.areaInteractor = PolygonInteractor(ax, msg.area.points, areaType, self._interactorEventHandler)
        self.obstacleInteractors = list[PolygonInteractor]()
        self._eventHandler = eventHandler
        
        for obstacle in msg.obstacles:
            self._addObstacleInteractor(obstacle.points)

    def getMsg(self):
        if self.areaInteractor == None or self.areaInteractor.poly == None:
            return None

        self._msg.area.points = []
        self._msg.obstacles = []

        for x,y in self.areaInteractor.poly.xy:
            point = Point32(x,y,0) 
            self._msg.area.points.append(point)
        
        for obstacleInteractor in self.obstacleInteractors:
            if obstacleInteractor.poly == None:
                continue

            obstacle = Polygon()
            for x,y in obstacleInteractor.poly.xy:
                point = Point32(x,y,0) 
                obstacle.points.append(point)
            
            self._msg.obstacles.append(obstacle)

        return self._msg
    
    def _addObstacleInteractor(self, points: list[Point32]):
        self.obstacleInteractors.append(PolygonInteractor(self._ax, points, AreaType.RESTRICTED_AREA, self._interactorEventHandler))

    def _removeObstacleInteractor(self, obstacleInteractor: PolygonInteractor):
        self.obstacleInteractors.remove(obstacleInteractor)

    def _interactorEventHandler(self, interactorEvent: InteractorEvent, arg = None):
        match interactorEvent:
            case InteractorEvent.RESTRICTED_AREA_ADDED:
                self._addObstacleInteractor(arg)
            case InteractorEvent.RESTRICTED_AREA_DELETED:
                self._removeObstacleInteractor(arg)
            case InteractorEvent.MOWING_AREA_DELETED:
                self._deleteAreaInteractor(interactorEvent)
            case InteractorEvent.NAVIGATION_AREA_DELETED:
                self._deleteAreaInteractor(interactorEvent)
    
    def _deleteAreaInteractor(self, interactorEvent: InteractorEvent):
        for obstacleInteractor in self.obstacleInteractors:
            obstacleInteractor.delete()
        self.obstacleInteractors = list[PolygonInteractor]()
        self.areaInteractor = None
        self._eventHandler(interactorEvent, self)

class Plot:
    def __init__(self):
        fig, axes = plt.subplots(1, 1)
        self.epsilon = 10 #5  # max pixel distance to count as a vertex hit
        self.canvas = fig.canvas
        self.axes = axes
        self.axes.set_title(AxesText.PLOT_TITLE.value, x=0, size=8, fontfamily='monospace', ha='left')
        self.axes.set_xlabel(AxesText.DEFAULT.value, x=0, size=8, fontfamily='monospace', ha='left')
        self.axes.set_xticks([], None)
        self.axes.set_yticks([], None)
        self.axes.set_aspect('equal')
        self.axes.format_coord = lambda x, y : f''
        self.timer = self.canvas.new_timer(interval=2000)
        self.canvas.zorder = 0
        self.canvas.manager.set_window_title('OpenMower Map editor')
        self.canvas.manager.toolmanager.remove_tool('back')
        self.canvas.manager.toolmanager.remove_tool('forward')
        self.canvas.manager.toolmanager.remove_tool('subplots')
        self.canvas.manager.toolmanager.remove_tool('save')
        self.canvas.manager.toolmanager.remove_tool('help')
        self.canvas.background = None

    def getPatchUnderTheMouse(self, event):
        patches = list[Patch]()
        for patch in self.axes.patches:
            if patch.get_visible() == False:
                continue
            if patch.contains_point([event.x, event.y]):
                patches.append(patch)
        
        if len(patches) == 0:
            return None

        zorderedPatches = sorted(patches, key=lambda patch: patch.zorder, reverse=True)

        return zorderedPatches[0]
    
    def setAxesText(self, axesText: AxesText):
        self.axes.set_xlabel(axesText.value, ha='left')

    def showStatusText(self, text, autohide=True):
        self.axes.set_title(text, x=0, size=8, fontfamily='monospace', ha='left')
        self.canvas.draw()
        if autohide == False:
            return
        self.timer.add_callback(self.hideStatusText)
        self.timer.start()

    def hideStatusText(self):
        self.axes.set_title(AxesText.PLOT_TITLE.value, x=0, size=8, fontfamily='monospace', ha='left')
        self.canvas.draw()
        self.timer.stop()

class MapStatus:
    status: Status = Status.IDLE
    areaInteractor1 = None
    areaInteractor2 = None

class Map:
    def __init__(self):
        self.plot = Plot()
        self._selectedAreaInteractor = None
        self._selectedPointIndex = None
        self._isMovingThePoint = False
        self._mapStatus = MapStatus()
        self.on_button_press_cid = self.plot.canvas.mpl_connect('button_press_event', self.on_button_press)
        self.on_key_press_cid = self.plot.canvas.mpl_connect('key_press_event', self.on_key_press)
        self.on_mouse_move_cid = self.plot.canvas.mpl_connect('motion_notify_event', self.on_mouse_move)

        self.dockingPoint = DockingPoint
        self.mowingAreas = list[Area]()
        self.navigationAreas = list[Area]()

    def addDockingPoint(self, msg):
        self.dockingPoint = DockingPoint(self.plot.axes, msg)

    def addMowingArea(self, msg):
        self.mowingAreas.append(Area(self.plot.axes, msg, AreaType.MOWING_AREA, self.eventHandler))

    def addNavigationArea(self, msg):
        self.navigationAreas.append(Area(self.plot.axes, msg, AreaType.NAVIGATION_AREA, self.eventHandler))

    def eventHandler(self, interactorEvent: InteractorEvent, arg):
        match interactorEvent:
            case InteractorEvent.MOWING_AREA_DELETED:
                self.mowingAreas.remove(arg)
            case InteractorEvent.NAVIGATION_AREA_DELETED:
                self.navigationAreas.remove(arg)

    def on_button_press(self, event):
        """Callback for mouse button presses."""
        if event.inaxes is None:
            return
        if event.button != 1:
            return      

        self._selectedAreaInteractor = self._getAreaInteractorUnderTheMouse(event)
        self._selectedPointIndex = self._getPointIndexUnderTheMouse(event, self._selectedAreaInteractor)

        if self._mapStatus.status != Status.IDLE:
            match self._mapStatus.status:
                case Status.CONVERT_SELECTING_SOURCE_AREA:
                    if self._selectedAreaInteractor != None and self._selectedAreaInteractor.areaType != AreaType.RESTRICTED_AREA:
                        self._mapStatus.areaInteractor1 = self._selectedAreaInteractor
                        self._mapStatus.status = Status.CONVERT_SELECTING_TARGET_AREA
                        self.plot.showStatusText(StatusBarText.SELECT_AREA_TO_ADD_RESTRICIONS.value, False)
                case Status.CONVERT_SELECTING_TARGET_AREA:
                    if self._selectedAreaInteractor != None and self._selectedAreaInteractor.areaType == AreaType.MOWING_AREA and self._selectedAreaInteractor != self._mapStatus.areaInteractor1:
                        self._mapStatus.areaInteractor2 = self._selectedAreaInteractor
                        answer = self._messageBox('Convert to Restricted Area', 'Are you sure you want to convert ' + str(self._selectedAreaInteractor.areaType) + ' to Restricted Area?',  MessageBoxStyle.YesNo)
                        if answer != 6:
                            self._mapStatus = Status.IDLE
                            self.plot.hideStatusText()
                            return
                        self._convertToRestrictedArea()
                        self.plot.showStatusText(StatusBarText.AREA_CONVERTED.value)
                case Status.MERGE_SELECTING_AREA:
                    if self._selectedAreaInteractor != None and self._selectedAreaInteractor.areaType == AreaType.MOWING_AREA:
                        self._mapStatus.areaInteractor1 = self._selectedAreaInteractor
                        self._mapStatus.status = Status.MERGE_SELECTING_MERGE_POINT
                        self.plot.showStatusText(StatusBarText.SELECT_POINT_TO_MERGE.value, False)
                case Status.MERGE_SELECTING_MERGE_POINT:
                    if self._selectedPointIndex != None and self._selectedAreaInteractor.areaType == AreaType.MOWING_AREA and self._selectedAreaInteractor != self._mapStatus.areaInteractor1:
                        self._mapStatus.areaInteractor2 = self._selectedAreaInteractor
                        answer = self._messageBox('Merge Areas', 'Are you sure you want to merge Mowing Areas?',  MessageBoxStyle.YesNo)
                        if answer != 6:
                            self._mapStatus = Status.IDLE
                            self.plot.hideStatusText()
                            return
                        self._mergeAreas(self._selectedPointIndex)
                        self.plot.showStatusText(StatusBarText.AREAS_MERGED.value)
            return

        if event.key != None and event.key.isdigit() and event.key != '0':
            if self._selectedAreaInteractor != None and self._selectedAreaInteractor.areaType == AreaType.MOWING_AREA:
                self._setMowingAreaIndex(self._selectedAreaInteractor, int(event.key))
        elif self._selectedPointIndex != None:
            self._selectedAreaInteractor.selectPoint(self._selectedPointIndex)
            self.plot.setAxesText(AxesText.POINT_SELECTED)
        else:
            self._deselectAll()
            self.plot.setAxesText(AxesText.DEFAULT)

    def on_key_press(self, event):
        """Callback for key presses."""
        if not event.inaxes:
            return

        if event.key == 'escape':
            self._mapStatus = Status.IDLE
            self.plot.hideStatusText()
            return
        if event.key == 'c':
            self._mapStatus.status = Status.CONVERT_SELECTING_SOURCE_AREA
            self.plot.showStatusText(StatusBarText.SELECT_AREA_TO_CONVERT.value, False)
        elif event.key == 'd':
            areaInteractor = self._getAreaInteractorUnderTheMouse(event)
            selectedPointIndex = self._getPointIndexUnderTheMouse(event,areaInteractor)

            if selectedPointIndex != None:
                areaInteractor.deletePoint(selectedPointIndex)
            elif areaInteractor != None:
                if areaInteractor.eventHandler == None:
                    return
                answer = self._messageBox('Delete ' + str(areaInteractor.areaType), 'Are you sure you want to delete ' + str(areaInteractor.areaType) + '?',  MessageBoxStyle.YesNo)
                if answer == 6:
                    areaInteractor.delete()
        elif event.key == 'h':
            areaInteractor = self._getAreaInteractorUnderTheMouse(event)
            if areaInteractor != None:
                areaInteractor.hide()
        elif event.key == 'i':         
            areaInteractor = self._getAreaInteractorUnderTheMouse(event)
            selectedPointIndex = self._getPointIndexUnderTheMouse(event,areaInteractor)

            if selectedPointIndex != None:
                areaInteractor.addPoint(selectedPointIndex)
                if self._selectedPointIndex is None:
                    return
                self._selectedPointIndex += 1
                areaInteractor.selectPoint(self._selectedPointIndex)
            elif areaInteractor != None:
                if areaInteractor.eventHandler == None:
                    return
                if areaInteractor.areaType == AreaType.DOCKING_POINT or areaInteractor.areaType == AreaType.RESTRICTED_AREA:
                    return
                areaInteractor.eventHandler(InteractorEvent.RESTRICTED_AREA_ADDED, [Point32(event.xdata - 1, event.ydata, 0), Point32(event.xdata, event.ydata - 1, 0), Point32(event.xdata, event.ydata + 1, 0)])
        elif event.key == 'm':
            self._mapStatus.status = Status.MERGE_SELECTING_AREA
            self.plot.showStatusText(StatusBarText.SELECT_AREA_TO_MERGE.value, False)
        else:
            if self._selectedPointIndex != None:
                self._selectedAreaInteractor.movePoint(self._selectedPointIndex, event.key)
           

    def on_mouse_move(self, event):
        """Callback for mouse movements."""
        if event.inaxes is None:
            return
        
        if event.button == 1:
            if self._selectedPointIndex != None:
                self._selectedAreaInteractor.movePoint(self._selectedPointIndex, 'mouse+move', event.xdata, event.ydata)
                self._isMovingThePoint = True
        else:
            if self._selectedPointIndex != None and self._isMovingThePoint == False:
                return
            areaInteractor = self._getAreaInteractorUnderTheMouse(event)
            selectedPointIndex = self._getPointIndexUnderTheMouse(event,areaInteractor)
            if selectedPointIndex != None:
                areaInteractor.selectPoint(selectedPointIndex)
            else:
                self._deselectAll()
                self._selectedPointIndex = None
                self._isMovingThePoint = False

    def _getAreaInteractorUnderTheMouse(self, event):
        patch = self.plot.getPatchUnderTheMouse(event)
        if patch == None:
            return None
        for mowingArea in self.mowingAreas:
            if mowingArea.areaInteractor.poly == patch:
                return mowingArea.areaInteractor
            
            for obstacleInteractor in mowingArea.obstacleInteractors:
                if obstacleInteractor.poly == patch:
                    return obstacleInteractor

        for navigationArea in self.navigationAreas:
            if navigationArea.areaInteractor.poly == patch:
                return navigationArea.areaInteractor
            
            for obstacleInteractor in navigationArea.obstacleInteractors:
                if obstacleInteractor.poly == patch:
                    return obstacleInteractor

        return None

    def _getPointIndexUnderTheMouse(self, event, areaInteractor: PolygonInteractor):
        """
        Return the index of the point closest to the event position or *None*
        if no point is within ``self.epsilon`` to the event position.
        """
        # display coords
        if areaInteractor == None:
            return None
        
        xy = np.asarray(areaInteractor.poly.xy)
        xyt = areaInteractor.poly.get_transform().transform(xy)
        xt, yt = xyt[:, 0], xyt[:, 1]
        d = np.hypot(xt - event.x, yt - event.y)
        indseq, = np.nonzero(d == d.min())
        ind = indseq[0]

        if d[ind] >= self.plot.epsilon:
            ind = None

        return ind

    def _deselectAll(self):
        for mowingArea in self.mowingAreas:
            mowingArea.areaInteractor.deselectPoint()
            for obstacleInteractor in mowingArea.obstacleInteractors:
                obstacleInteractor.deselectPoint()

        for navigationArea in self.navigationAreas:
            navigationArea.areaInteractor.deselectPoint()
            for obstacleInteractor in navigationArea.obstacleInteractors:
                obstacleInteractor.deselectPoint()

    def _mergeAreas(self, mergePointIndex):
        xys = self._mapStatus.areaInteractor2.poly.xy
        index = 0
        for xy in self._mapStatus.areaInteractor1.poly.xy:
            xys = np.insert(xys, mergePointIndex + index, xy, axis=0)
            index += 1

        self._mapStatus.areaInteractor2.addPoints(xys)
        self._mapStatus.areaInteractor1.delete()

    def _convertToRestrictedArea(self):
        points = list[Point32]()
        for xy in self._mapStatus.areaInteractor1.poly.xy:
            points.append(Point32(xy[0], xy[1], 0))
        self._mapStatus.areaInteractor2.eventHandler(InteractorEvent.RESTRICTED_AREA_ADDED, points)
        self._mapStatus.areaInteractor1.delete()
        
    def _setMowingAreaIndex(self, areaInteractor: PolygonInteractor, index: int):
        mowingAreasLen = len(self.mowingAreas) - 1
        for mowingArea in self.mowingAreas:
            if mowingArea.areaInteractor == areaInteractor:
                self.mowingAreas.insert(min(mowingAreasLen, index - 1), self.mowingAreas.pop(self.mowingAreas.index(mowingArea)))
                self.plot.showStatusText(StatusBarText.MOWING_AREA_ORDER.value + StatusBarText(min(mowingAreasLen + 1, index)).name + StatusBarText.MOWING_AREA_TO_MOW.value)
                break

    def _messageBox(self, title: str, text: str, style: MessageBoxStyle):
        return ctypes.windll.user32.MessageBoxW(0, text, title, style.value)

def dist(x, y):
    """
    Return the distance between two points.
    """
    d = x - y
    return np.sqrt(np.dot(d, d))

# def dist_point_to_segment(p, s0, s1):
#     """
#     Get the distance of a point to a segment.
#       *p*, *s0*, *s1* are *xy* sequences
#     This algorithm from
#     http://www.geomalgorithms.com/algorithms.html
#     """
#     v = s1 - s0
#     w = p - s0
#     c1 = np.dot(w, v)
#     if c1 <= 0:
#         return dist(p, s0)
#     c2 = np.dot(v, v)
#     if c2 <= c1:
#         return dist(p, s1)
#     b = c1 / c2
#     pb = s0 + b * v
#     return dist(p, pb)  

if __name__ == '__main__':
    bag = rosbag.Bag('output1.bag')
    with rosbag.Bag('output3.bag', 'w') as outbag:
        plt.rcParams['toolbar'] = 'toolmanager'
        plt.rcParams["figure.autolayout"] = True
        
        map = Map()

        for topic, msg, t in bag.read_messages():
            match topic:
                case 'docking_point':
                    map.addDockingPoint(msg)
                case 'mowing_areas':
                    map.addMowingArea(msg)
                case 'navigation_areas':
                    map.addNavigationArea(msg)

        plt.show()
        #figure closed by user, get the new data

        if map.dockingPoint != None:
            outbag.write('docking_point', map.dockingPoint.getMsg())

        for mowingArea in map.mowingAreas:
            outbag.write('mowing_areas', mowingArea.getMsg())

        for navigationArea in map.navigationAreas:
            outbag.write('navigation_areas', navigationArea.getMsg())