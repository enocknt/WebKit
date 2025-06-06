/*
 * Copyright (C) 2012, 2015 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "ScrollingStateScrollingNode.h"

#if ENABLE(ASYNC_SCROLLING)

#include "ScrollingStateTree.h"
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/TextStream.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(ScrollingStateScrollingNode);

ScrollingStateScrollingNode::ScrollingStateScrollingNode(ScrollingStateTree& stateTree, ScrollingNodeType nodeType, ScrollingNodeID nodeID)
    : ScrollingStateNode(nodeType, stateTree, nodeID)
{
    scrollingStateTree().scrollingNodeAdded();
}

ScrollingStateScrollingNode::ScrollingStateScrollingNode(
    ScrollingNodeType nodeType,
    ScrollingNodeID nodeID,
    Vector<Ref<ScrollingStateNode>>&& children,
    OptionSet<ScrollingStateNodeProperty> changedProperties,
    std::optional<PlatformLayerIdentifier> layerID,
    FloatSize scrollableAreaSize,
    FloatSize totalContentsSize,
    FloatSize reachableContentsSize,
    FloatPoint scrollPosition,
    IntPoint scrollOrigin,
    ScrollableAreaParameters&& scrollableAreaParameters,
#if ENABLE(SCROLLING_THREAD)
    OptionSet<SynchronousScrollingReason> synchronousScrollingReasons,
#endif
    RequestedScrollData&& requestedScrollData,
    FloatScrollSnapOffsetsInfo&& snapOffsetsInfo,
    std::optional<unsigned> currentHorizontalSnapPointIndex,
    std::optional<unsigned> currentVerticalSnapPointIndex,
    bool isMonitoringWheelEvents,
    std::optional<PlatformLayerIdentifier> scrollContainerLayer,
    std::optional<PlatformLayerIdentifier> scrolledContentsLayer,
    std::optional<PlatformLayerIdentifier> horizontalScrollbarLayer,
    std::optional<PlatformLayerIdentifier> verticalScrollbarLayer,
    bool mouseIsOverContentArea,
    MouseLocationState&& mouseLocationState,
    ScrollbarHoverState&& scrollbarHoverState,
    ScrollbarEnabledState&& scrollbarEnabledState,
    UserInterfaceLayoutDirection scrollbarLayoutDirection,
    ScrollbarWidth scrollbarWidth,
    bool useDarkAppearanceForScrollbars,
    RequestedKeyboardScrollData&& keyboardScrollData
) : ScrollingStateNode(nodeType, nodeID, WTFMove(children), changedProperties, layerID)
    , m_scrollableAreaSize(scrollableAreaSize)
    , m_totalContentsSize(totalContentsSize)
    , m_reachableContentsSize(reachableContentsSize)
    , m_scrollPosition(scrollPosition)
    , m_scrollOrigin(scrollOrigin)
    , m_snapOffsetsInfo(WTFMove(snapOffsetsInfo))
    , m_currentHorizontalSnapPointIndex(currentHorizontalSnapPointIndex)
    , m_currentVerticalSnapPointIndex(currentVerticalSnapPointIndex)
    , m_scrollContainerLayer(scrollContainerLayer)
    , m_scrolledContentsLayer(scrolledContentsLayer)
    , m_horizontalScrollbarLayer(horizontalScrollbarLayer)
    , m_verticalScrollbarLayer(verticalScrollbarLayer)
    , m_scrollbarHoverState(WTFMove(scrollbarHoverState))
    , m_mouseLocationState(WTFMove(mouseLocationState))
    , m_scrollbarEnabledState(WTFMove(scrollbarEnabledState))
    , m_scrollableAreaParameters(WTFMove(scrollableAreaParameters))
    , m_requestedScrollData(WTFMove(requestedScrollData))
    , m_keyboardScrollData(WTFMove(keyboardScrollData))
#if ENABLE(SCROLLING_THREAD)
    , m_synchronousScrollingReasons(synchronousScrollingReasons)
#endif
    , m_scrollbarLayoutDirection(scrollbarLayoutDirection)
    , m_scrollbarWidth(scrollbarWidth)
    , m_useDarkAppearanceForScrollbars(useDarkAppearanceForScrollbars)
    , m_isMonitoringWheelEvents(isMonitoringWheelEvents)
    , m_mouseIsOverContentArea(mouseIsOverContentArea)
{
    // scrollingNodeAdded will be called in attachAfterDeserialization.
}

ScrollingStateScrollingNode::ScrollingStateScrollingNode(const ScrollingStateScrollingNode& stateNode, ScrollingStateTree& adoptiveTree)
    : ScrollingStateNode(stateNode, adoptiveTree)
    , m_scrollableAreaSize(stateNode.scrollableAreaSize())
    , m_totalContentsSize(stateNode.totalContentsSize())
    , m_reachableContentsSize(stateNode.reachableContentsSize())
    , m_scrollPosition(stateNode.scrollPosition())
    , m_scrollOrigin(stateNode.scrollOrigin())
    , m_snapOffsetsInfo(stateNode.m_snapOffsetsInfo)
#if PLATFORM(MAC)
    , m_scrollbarHoverState(stateNode.scrollbarHoverState())
    , m_mouseLocationState(stateNode.mouseLocationState())
    , m_scrollbarEnabledState(stateNode.scrollbarEnabledState())
    , m_verticalScrollerImp(stateNode.verticalScrollerImp())
    , m_horizontalScrollerImp(stateNode.horizontalScrollerImp())
#endif
    , m_scrollableAreaParameters(stateNode.scrollableAreaParameters())
    , m_requestedScrollData(stateNode.requestedScrollData())
    , m_keyboardScrollData(stateNode.keyboardScrollData())
#if ENABLE(SCROLLING_THREAD)
    , m_synchronousScrollingReasons(stateNode.synchronousScrollingReasons())
#endif
    , m_scrollbarLayoutDirection(stateNode.scrollbarLayoutDirection())
    , m_scrollbarWidth(stateNode.scrollbarWidth())
    , m_useDarkAppearanceForScrollbars(stateNode.useDarkAppearanceForScrollbars())
    , m_isMonitoringWheelEvents(stateNode.isMonitoringWheelEvents())
    , m_mouseIsOverContentArea(stateNode.mouseIsOverContentArea())
{
    scrollingStateTree().scrollingNodeAdded();

    if (hasChangedProperty(Property::ScrollContainerLayer))
        setScrollContainerLayer(stateNode.scrollContainerLayer().toRepresentation(adoptiveTree.preferredLayerRepresentation()));

    if (hasChangedProperty(Property::ScrolledContentsLayer))
        setScrolledContentsLayer(stateNode.scrolledContentsLayer().toRepresentation(adoptiveTree.preferredLayerRepresentation()));

    if (hasChangedProperty(Property::VerticalScrollbarLayer))
        setVerticalScrollbarLayer(stateNode.verticalScrollbarLayer().toRepresentation(adoptiveTree.preferredLayerRepresentation()));

    if (hasChangedProperty(Property::HorizontalScrollbarLayer))
        setHorizontalScrollbarLayer(stateNode.horizontalScrollbarLayer().toRepresentation(adoptiveTree.preferredLayerRepresentation()));
}

ScrollingStateScrollingNode::~ScrollingStateScrollingNode()
{
    if (isAttachedToScrollingStateTree())
        scrollingStateTree().scrollingNodeRemoved();
}

OptionSet<ScrollingStateNode::Property> ScrollingStateScrollingNode::applicableProperties() const
{
    // Note that this list does not include Property::RequestedScrollPosition, which is imperative, not stateful.
    constexpr OptionSet<Property> nodeProperties = {
        Property::ScrollableAreaSize,
        Property::TotalContentsSize,
        Property::ReachableContentsSize,
        Property::ScrollPosition,
        Property::ScrollOrigin,
        Property::ScrollableAreaParams,
#if ENABLE(SCROLLING_THREAD)
        Property::ReasonsForSynchronousScrolling,
#endif
        Property::SnapOffsetsInfo,
        Property::CurrentHorizontalSnapOffsetIndex,
        Property::CurrentVerticalSnapOffsetIndex,
        Property::IsMonitoringWheelEvents,
        Property::ScrollContainerLayer,
        Property::ScrolledContentsLayer,
        Property::HorizontalScrollbarLayer,
        Property::VerticalScrollbarLayer,
        Property::PainterForScrollbar
    };

    auto properties = ScrollingStateNode::applicableProperties();
    properties.add(nodeProperties);
    return properties;
}

void ScrollingStateScrollingNode::setScrollableAreaSize(const FloatSize& size)
{
    if (m_scrollableAreaSize == size)
        return;

    m_scrollableAreaSize = size;
    setPropertyChanged(Property::ScrollableAreaSize);
}

void ScrollingStateScrollingNode::setTotalContentsSize(const FloatSize& totalContentsSize)
{
    if (m_totalContentsSize == totalContentsSize)
        return;

    m_totalContentsSize = totalContentsSize;
    setPropertyChanged(Property::TotalContentsSize);
}

void ScrollingStateScrollingNode::setReachableContentsSize(const FloatSize& reachableContentsSize)
{
    if (m_reachableContentsSize == reachableContentsSize)
        return;

    m_reachableContentsSize = reachableContentsSize;
    setPropertyChanged(Property::ReachableContentsSize);
}

void ScrollingStateScrollingNode::setScrollPosition(const FloatPoint& scrollPosition)
{
    if (m_scrollPosition == scrollPosition)
        return;

    m_scrollPosition = scrollPosition;
    setPropertyChanged(Property::ScrollPosition);
}

void ScrollingStateScrollingNode::setScrollOrigin(const IntPoint& scrollOrigin)
{
    if (m_scrollOrigin == scrollOrigin)
        return;

    m_scrollOrigin = scrollOrigin;
    setPropertyChanged(Property::ScrollOrigin);
}

void ScrollingStateScrollingNode::setSnapOffsetsInfo(const FloatScrollSnapOffsetsInfo& info)
{
    if (m_snapOffsetsInfo.isEqual(info))
        return;

    m_snapOffsetsInfo = info;
    setPropertyChanged(Property::SnapOffsetsInfo);
}

void ScrollingStateScrollingNode::setCurrentHorizontalSnapPointIndex(std::optional<unsigned> index)
{
    if (m_currentHorizontalSnapPointIndex == index)
        return;
    
    m_currentHorizontalSnapPointIndex = index;
    setPropertyChanged(Property::CurrentHorizontalSnapOffsetIndex);
}

void ScrollingStateScrollingNode::setCurrentVerticalSnapPointIndex(std::optional<unsigned> index)
{
    if (m_currentVerticalSnapPointIndex == index)
        return;
    
    m_currentVerticalSnapPointIndex = index;
    setPropertyChanged(Property::CurrentVerticalSnapOffsetIndex);
}

void ScrollingStateScrollingNode::setScrollableAreaParameters(const ScrollableAreaParameters& parameters)
{
    if (m_scrollableAreaParameters == parameters)
        return;

    m_scrollableAreaParameters = parameters;
    setPropertyChanged(Property::ScrollableAreaParams);
}

#if ENABLE(SCROLLING_THREAD)
void ScrollingStateScrollingNode::setSynchronousScrollingReasons(OptionSet<SynchronousScrollingReason> reasons)
{
    if (m_synchronousScrollingReasons == reasons)
        return;

    m_synchronousScrollingReasons = reasons;
    setPropertyChanged(Property::ReasonsForSynchronousScrolling);
}
#endif


void ScrollingStateScrollingNode::setKeyboardScrollData(const RequestedKeyboardScrollData& scrollData)
{
    m_keyboardScrollData = scrollData;
    setPropertyChanged(Property::KeyboardScrollData);
}

void ScrollingStateScrollingNode::setRequestedScrollData(RequestedScrollData&& scrollData, CanMergeScrollData canMergeScrollData)
{
    // Scroll position requests are imperative, not stateful, so we can't early return here.
    if (hasChangedProperty(Property::RequestedScrollPosition) && canMergeScrollData == CanMergeScrollData::Yes) {
        m_requestedScrollData.merge(WTFMove(scrollData));
        return;
    }

    m_requestedScrollData = WTFMove(scrollData);
    setPropertyChanged(Property::RequestedScrollPosition);
}

bool ScrollingStateScrollingNode::hasScrollPositionRequest() const
{
    return hasChangedProperty(Property::RequestedScrollPosition) && m_requestedScrollData.requestType != ScrollRequestType::CancelAnimatedScroll;
}

void ScrollingStateScrollingNode::setIsMonitoringWheelEvents(bool isMonitoringWheelEvents)
{
    if (isMonitoringWheelEvents == m_isMonitoringWheelEvents)
        return;

    m_isMonitoringWheelEvents = isMonitoringWheelEvents;
    setPropertyChanged(Property::IsMonitoringWheelEvents);
}

void ScrollingStateScrollingNode::setScrollContainerLayer(const LayerRepresentation& layerRepresentation)
{
    if (layerRepresentation == m_scrollContainerLayer)
        return;

    m_scrollContainerLayer = layerRepresentation;
    setPropertyChanged(Property::ScrollContainerLayer);
}

void ScrollingStateScrollingNode::setScrolledContentsLayer(const LayerRepresentation& layerRepresentation)
{
    if (layerRepresentation == m_scrolledContentsLayer)
        return;

    m_scrolledContentsLayer = layerRepresentation;
    setPropertyChanged(Property::ScrolledContentsLayer);
}

void ScrollingStateScrollingNode::setHorizontalScrollbarLayer(const LayerRepresentation& layer)
{
    if (layer == m_horizontalScrollbarLayer)
        return;

    m_horizontalScrollbarLayer = layer;
    setPropertyChanged(Property::HorizontalScrollbarLayer);
}

void ScrollingStateScrollingNode::setVerticalScrollbarLayer(const LayerRepresentation& layer)
{
    if (layer == m_verticalScrollbarLayer)
        return;
    m_verticalScrollbarLayer = layer;
    setPropertyChanged(Property::VerticalScrollbarLayer);
}

#if !PLATFORM(MAC)
void ScrollingStateScrollingNode::setScrollerImpsFromScrollbars(Scrollbar*, Scrollbar*)
{
}
#endif

void ScrollingStateScrollingNode::setMouseIsOverContentArea(bool flag)
{
    if (flag == m_mouseIsOverContentArea)
        return;

    m_mouseIsOverContentArea = flag;
    setPropertyChanged(Property::ContentAreaHoverState);
}

void ScrollingStateScrollingNode::setMouseMovedInContentArea(const MouseLocationState& mouseLocationState)
{
    m_mouseLocationState = mouseLocationState;
    setPropertyChanged(Property::MouseActivityState);
}
    
void ScrollingStateScrollingNode::setScrollbarHoverState(ScrollbarHoverState hoverState)
{
    if (hoverState == m_scrollbarHoverState)
        return;

    m_scrollbarHoverState = hoverState;
    setPropertyChanged(Property::ScrollbarHoverState);
}

void ScrollingStateScrollingNode::setScrollbarEnabledState(ScrollbarOrientation orientation, bool enabled)
{
    if ((orientation == ScrollbarOrientation::Horizontal ? m_scrollbarEnabledState.horizontalScrollbarIsEnabled : m_scrollbarEnabledState.verticalScrollbarIsEnabled) == enabled)
        return;

    if (orientation == ScrollbarOrientation::Horizontal)
        m_scrollbarEnabledState.horizontalScrollbarIsEnabled = enabled;
    else
        m_scrollbarEnabledState.verticalScrollbarIsEnabled = enabled;

    setPropertyChanged(Property::ScrollbarEnabledState);
}

void ScrollingStateScrollingNode::setScrollbarLayoutDirection(UserInterfaceLayoutDirection scrollbarLayoutDirection)
{
    if (scrollbarLayoutDirection == m_scrollbarLayoutDirection)
        return;

    m_scrollbarLayoutDirection = scrollbarLayoutDirection;
    setPropertyChanged(Property::ScrollbarLayoutDirection);
}

void ScrollingStateScrollingNode::setScrollbarWidth(ScrollbarWidth scrollbarWidth)
{
    if (scrollbarWidth == m_scrollbarWidth)
        return;
    m_scrollbarWidth = scrollbarWidth;
    setPropertyChanged(Property::ScrollbarWidth);
}

void ScrollingStateScrollingNode::setUseDarkAppearanceForScrollbars(bool useDarkAppearanceForScrollbars)
{
    if (useDarkAppearanceForScrollbars == m_useDarkAppearanceForScrollbars)
        return;
    m_useDarkAppearanceForScrollbars = useDarkAppearanceForScrollbars;
    setPropertyChanged(Property::UseDarkAppearanceForScrollbars);
}

void ScrollingStateScrollingNode::dumpProperties(TextStream& ts, OptionSet<ScrollingStateTreeAsTextBehavior> behavior) const
{
    ScrollingStateNode::dumpProperties(ts, behavior);
    
    if (m_scrollPosition != FloatPoint()) {
        TextStream::GroupScope scope(ts);
        ts << "scroll position "_s
            << TextStream::FormatNumberRespectingIntegers(m_scrollPosition.x()) << " "
            << TextStream::FormatNumberRespectingIntegers(m_scrollPosition.y());
    }

    if (!m_scrollableAreaSize.isEmpty()) {
        TextStream::GroupScope scope(ts);
        ts << "scrollable area size "_s
            << TextStream::FormatNumberRespectingIntegers(m_scrollableAreaSize.width()) << " "
            << TextStream::FormatNumberRespectingIntegers(m_scrollableAreaSize.height());
    }

    if (!m_totalContentsSize.isEmpty()) {
        TextStream::GroupScope scope(ts);
        ts << "contents size "_s
            << TextStream::FormatNumberRespectingIntegers(m_totalContentsSize.width()) << " "
            << TextStream::FormatNumberRespectingIntegers(m_totalContentsSize.height());
    }

    if (m_reachableContentsSize != m_totalContentsSize)
        ts.dumpProperty("reachable contents size"_s, m_reachableContentsSize);

    if (m_requestedScrollData.requestType == ScrollRequestType::PositionUpdate) {
        auto scrollPosition = std::get<FloatPoint>(m_requestedScrollData.scrollPositionOrDelta);
        if (!scrollPosition.isZero()) {
            TextStream::GroupScope scope(ts);
            ts << "requested scroll position "_s
            << TextStream::FormatNumberRespectingIntegers(scrollPosition.x()) << " "
            << TextStream::FormatNumberRespectingIntegers(scrollPosition.y());
        }
    } else if (m_requestedScrollData.requestType == ScrollRequestType::DeltaUpdate) {
        auto scrollDelta = std::get<FloatSize>(m_requestedScrollData.scrollPositionOrDelta);
        if (!scrollDelta.isZero()) {
            TextStream::GroupScope scope(ts);
            ts << "requested scroll delta "_s
            << TextStream::FormatNumberRespectingIntegers(scrollDelta.width()) << " "
            << TextStream::FormatNumberRespectingIntegers(scrollDelta.height());
        }
    }

    if (m_requestedScrollData.scrollType == ScrollType::Programmatic)
        ts.dumpProperty("requested scroll position represents programmatic scroll"_s, true);

    if (m_requestedScrollData.clamping == ScrollClamping::Unclamped)
        ts.dumpProperty("requested scroll position clamping"_s, m_requestedScrollData.clamping);

    if (m_requestedScrollData.animated == ScrollIsAnimated::Yes)
        ts.dumpProperty("requested scroll position is animated"_s, true);

    if (m_scrollOrigin != IntPoint())
        ts.dumpProperty("scroll origin"_s, m_scrollOrigin);

    if (m_snapOffsetsInfo.horizontalSnapOffsets.size())
        ts.dumpProperty("horizontal snap offsets"_s, m_snapOffsetsInfo.horizontalSnapOffsets);

    if (m_snapOffsetsInfo.verticalSnapOffsets.size())
        ts.dumpProperty("vertical snap offsets"_s, m_snapOffsetsInfo.verticalSnapOffsets);

    if (m_currentHorizontalSnapPointIndex)
        ts.dumpProperty("current horizontal snap point index"_s, m_currentHorizontalSnapPointIndex);

    if (m_currentVerticalSnapPointIndex)
        ts.dumpProperty("current vertical snap point index"_s, m_currentVerticalSnapPointIndex);

    ts.dumpProperty("scrollable area parameters"_s, m_scrollableAreaParameters);

#if ENABLE(SCROLLING_THREAD)
    if (!m_synchronousScrollingReasons.isEmpty())
        ts.dumpProperty("Scrolling on main thread because:"_s, ScrollingCoordinator::synchronousScrollingReasonsAsText(m_synchronousScrollingReasons));
#endif

    if (m_isMonitoringWheelEvents)
        ts.dumpProperty("expects wheel event test trigger"_s, m_isMonitoringWheelEvents);

    if (behavior & ScrollingStateTreeAsTextBehavior::IncludeLayerIDs) {
        if (m_scrollContainerLayer.layerID())
            ts.dumpProperty("scroll container layer"_s, m_scrollContainerLayer.layerID());
        if (m_scrolledContentsLayer.layerID())
            ts.dumpProperty("scrolled contents layer"_s, m_scrolledContentsLayer.layerID());
    }
}

} // namespace WebCore

#endif // ENABLE(ASYNC_SCROLLING)
