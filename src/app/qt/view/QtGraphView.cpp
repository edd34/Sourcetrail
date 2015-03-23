#include "QtGraphView.h"

#include <QBoxLayout>
#include <QFrame>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QSequentialAnimationGroup>

#include "qt/utility/QtGraphPostprocessor.h"
#include "qt/utility/utilityQt.h"

#include "qt/view/QtViewWidgetWrapper.h"
#include "qt/view/graphElements/nodeComponents/QtGraphNodeComponentClickable.h"
#include "qt/view/graphElements/nodeComponents/QtGraphNodeComponentMoveable.h"
#include "qt/view/graphElements/QtGraphEdge.h"
#include "qt/view/graphElements/QtGraphNode.h"
#include "qt/view/graphElements/QtGraphNodeAccess.h"

QtGraphView::QtGraphView(ViewLayout* viewLayout)
	: GraphView(viewLayout)
	, m_rebuildGraphFunctor(
		std::bind(&QtGraphView::doRebuildGraph, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3))
	, m_clearFunctor(std::bind(&QtGraphView::doClear, this))
	, m_resizeFunctor(std::bind(&QtGraphView::doResize, this))
{
}

QtGraphView::~QtGraphView()
{
}

void QtGraphView::createWidgetWrapper()
{
	setWidgetWrapper(std::make_shared<QtViewWidgetWrapper>(new QFrame()));
}

void QtGraphView::initView()
{
	QWidget* widget = QtViewWidgetWrapper::getWidgetOfView(this);
	utility::setWidgetBackgroundColor(widget, Colori(255, 255, 255, 255));

	QBoxLayout* layout = new QBoxLayout(QBoxLayout::TopToBottom);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	widget->setLayout(layout);

	QGraphicsScene* scene = new QGraphicsScene(widget);
	QGraphicsView* view = new QGraphicsView(widget);
	view->setScene(scene);
	view->setDragMode(QGraphicsView::ScrollHandDrag);

	widget->layout()->addWidget(view);
}

void QtGraphView::refreshView()
{
}

void QtGraphView::rebuildGraph(
	std::shared_ptr<Graph> graph,
	const std::vector<DummyNode>& nodes,
	const std::vector<DummyEdge>& edges
){
	m_rebuildGraphFunctor(graph, nodes, edges);
}

void QtGraphView::clear()
{
	m_clearFunctor();
}

void QtGraphView::resizeView()
{
	m_resizeFunctor();
}

Vec2i QtGraphView::getViewSize() const
{
	QGraphicsView* view = getView();
	return Vec2i(view->width(), view->height());
}

void QtGraphView::finishedTransition()
{
	for (const std::shared_ptr<QtGraphNode>& node : m_nodes)
	{
		node->setShadowEnabledRecursive(true);
	}

	QGraphicsView* view = getView();
	view->setInteractive(true);

	switchToNewGraphData();
}

void QtGraphView::switchToNewGraphData()
{
	m_oldGraph = m_graph;

	m_oldNodes = m_nodes;
	m_oldEdges = m_edges;

	m_nodes.clear();
	m_edges.clear();

	doResize();

	// Manually hover the item below the mouse cursor.
	QGraphicsView* view = getView();
	QPointF point = view->mapToScene(view->mapFromGlobal(QCursor::pos()));
	QGraphicsItem* item = view->scene()->itemAt(point, QTransform());
	if (item)
	{
		QtGraphNode* node = dynamic_cast<QtGraphNode*>(item->parentItem());
		if (node)
		{
			node->hoverEnter();
		}
	}
}

QGraphicsView* QtGraphView::getView() const
{
	QWidget* widget = QtViewWidgetWrapper::getWidgetOfView(this);

	QGraphicsView* view = widget->findChild<QGraphicsView*>("");

	if (!view)
	{
		LOG_ERROR("Failed to get QGraphicsView");
	}

	return view;
}

void QtGraphView::doRebuildGraph(
	std::shared_ptr<Graph> graph,
	const std::vector<DummyNode>& nodes,
	const std::vector<DummyEdge>& edges
){
	QGraphicsView* view = getView();

	m_nodes.clear();
	for (unsigned int i = 0; i < nodes.size(); i++)
	{
		std::shared_ptr<QtGraphNode> node = createNodeRecursive(view, NULL, nodes[i]);
		if (node)
		{
			m_nodes.push_back(node);
		}
	}

	QtGraphPostprocessor::doPostprocessing(m_nodes);

	QPointF center = itemsBoundingRect(m_nodes).center();
	Vec2i o = QtGraphPostprocessor::alignOnRaster(Vec2i(center.x(), center.y()));
	QPointF offset = QPointF(o.x, o.y);
	m_sceneRectOffset = offset - center;

	for (const std::shared_ptr<QtGraphNode>& node : m_nodes)
	{
		node->setPos(node->pos() - offset);
	}

	m_edges.clear();
	for (unsigned int i = 0; i < edges.size(); i++)
	{
		std::shared_ptr<QtGraphEdge> edge = createEdge(view, edges[i]);
		if (edge)
		{
			m_edges.push_back(edge);
		}
	}

	if (graph)
	{
		m_graph = graph;
	}

	createTransition();
}

void QtGraphView::doClear()
{
	m_nodes.clear();
	m_edges.clear();

	m_oldNodes.clear();
	m_oldEdges.clear();

	m_graph.reset();
	m_oldGraph.reset();
}

void QtGraphView::doResize()
{
	int margin = 25;
	QGraphicsView* view = getView();
	view->setSceneRect(itemsBoundingRect(m_oldNodes).adjusted(-margin, -margin, margin, margin).translated(m_sceneRectOffset));
}

std::shared_ptr<QtGraphNode> QtGraphView::findNodeRecursive(const std::list<std::shared_ptr<QtGraphNode>>& nodes, Id tokenId)
{
	for (const std::shared_ptr<QtGraphNode>& node : nodes)
	{
		if (node->getTokenId() == tokenId)
		{
			return node;
		}

		std::shared_ptr<QtGraphNode> result = findNodeRecursive(node->getSubNodes(), tokenId);
		if (result != NULL)
		{
			return result;
		}
	}

	return std::shared_ptr<QtGraphNode>();
}

std::shared_ptr<QtGraphNode> QtGraphView::createNodeRecursive(
	QGraphicsView* view, std::shared_ptr<QtGraphNode> parentNode, const DummyNode& node
){
	if (!node.visible)
	{
		return NULL;
	}

	std::shared_ptr<QtGraphNode> newNode;
	if (node.data)
	{
		newNode = std::make_shared<QtGraphNode>(node.data);
	}
	else
	{
		newNode = std::make_shared<QtGraphNodeAccess>(node.accessType, node.isExpanded(), node.invisibleSubNodeCount);
	}

	newNode->setPosition(node.position);
	newNode->setSize(node.size);
	newNode->setIsActive(node.active);

	newNode->addComponent(std::make_shared<QtGraphNodeComponentClickable>(newNode));

	view->scene()->addItem(newNode.get());

	if (parentNode)
	{
		newNode->setParent(parentNode);
	}
	else
	{
		newNode->addComponent(std::make_shared<QtGraphNodeComponentMoveable>(newNode));
	}

	for (unsigned int i = 0; i < node.subNodes.size(); i++)
	{
		std::shared_ptr<QtGraphNode> subNode = createNodeRecursive(view, newNode, node.subNodes[i]);
		if (subNode)
		{
			newNode->addSubNode(subNode);
		}
	}

	newNode->updateStyle();

	return newNode;
}

std::shared_ptr<QtGraphEdge> QtGraphView::createEdge(QGraphicsView* view, const DummyEdge& edge)
{
	if (!edge.visible)
	{
		return NULL;
	}

	std::shared_ptr<QtGraphNode> owner = findNodeRecursive(m_nodes, edge.ownerId);
	std::shared_ptr<QtGraphNode> target = findNodeRecursive(m_nodes, edge.targetId);

	if (owner != NULL && target != NULL)
	{
		std::shared_ptr<QtGraphEdge> qtEdge = std::make_shared<QtGraphEdge>(owner, target, edge.data);
		qtEdge->setIsActive(edge.active);

		owner->addOutEdge(qtEdge);
		target->addInEdge(qtEdge);

		view->scene()->addItem(qtEdge.get());

		return qtEdge;
	}
	else
	{
		LOG_WARNING_STREAM(<< "Couldn't find owner or target node for edge: " << edge.data->getName());
		return NULL;
	}
}

template <typename T>
QRectF QtGraphView::itemsBoundingRect(const std::list<std::shared_ptr<T>>& items) const
{
	QRectF boundingRect;
	for (const std::shared_ptr<T>& item : items)
	{
		boundingRect |= item->sceneBoundingRect();
	}
	return boundingRect;
}

void QtGraphView::compareNodesRecursive(
	std::list<std::shared_ptr<QtGraphNode>> newSubNodes,
	std::list<std::shared_ptr<QtGraphNode>> oldSubNodes,
	std::list<QtGraphNode*>* appearingNodes,
	std::list<QtGraphNode*>* vanishingNodes,
	std::vector<std::pair<QtGraphNode*, QtGraphNode*>>* remainingNodes
){
	for (std::list<std::shared_ptr<QtGraphNode>>::iterator it = newSubNodes.begin(); it != newSubNodes.end(); it++)
	{
		bool remains = false;

		for (std::list<std::shared_ptr<QtGraphNode>>::iterator it2 = oldSubNodes.begin(); it2 != oldSubNodes.end(); it2++)
		{
			if (((*it)->getTokenId() && (*it)->getTokenId() == (*it2)->getTokenId()) ||
				((*it)->isAccessNode() && (*it2)->isAccessNode() &&
					dynamic_cast<QtGraphNodeAccess*>((*it).get())->getAccessType() ==
						dynamic_cast<QtGraphNodeAccess*>((*it2).get())->getAccessType()))
			{
				remainingNodes->push_back(std::pair<QtGraphNode*, QtGraphNode*>((*it).get(), (*it2).get()));
				compareNodesRecursive((*it)->getSubNodes(), (*it2)->getSubNodes(), appearingNodes, vanishingNodes, remainingNodes);

				oldSubNodes.erase(it2);
				remains = true;
				break;
			}
		}

		if (!remains)
		{
			appearingNodes->push_back((*it).get());
		}
	}

	for (std::shared_ptr<QtGraphNode>& node : oldSubNodes)
	{
		vanishingNodes->push_back(node.get());
	}
}

void QtGraphView::createTransition()
{
	std::list<QtGraphNode*> appearingNodes;
	std::list<QtGraphNode*> vanishingNodes;
	std::vector<std::pair<QtGraphNode*, QtGraphNode*>> remainingNodes;

	compareNodesRecursive(m_nodes, m_oldNodes, &appearingNodes, &vanishingNodes, &remainingNodes);

	if (!vanishingNodes.size() && !appearingNodes.size())
	{
		switchToNewGraphData();
		return;
	}

	for (const std::shared_ptr<QtGraphNode>& node : m_nodes)
	{
		node->setShadowEnabledRecursive(false);
	}

	for (const std::shared_ptr<QtGraphNode>& node : m_oldNodes)
	{
		node->setShadowEnabledRecursive(false);
	}

	QGraphicsView* view = getView();
	view->setInteractive(false);

	m_transition = std::make_shared<QSequentialAnimationGroup>();

	// fade out
	if (vanishingNodes.size() || m_oldEdges.size())
	{
		QParallelAnimationGroup* vanish = new QParallelAnimationGroup();

		for (QtGraphNode* node : vanishingNodes)
		{
			QPropertyAnimation* anim = new QPropertyAnimation(node, "opacity");
			anim->setDuration(300);
			anim->setStartValue(1.0f);
			anim->setEndValue(0.0f);

			vanish->addAnimation(anim);
		}

		for (std::shared_ptr<QtGraphEdge> edge : m_oldEdges)
		{
			QPropertyAnimation* anim = new QPropertyAnimation(edge.get(), "opacity");
			anim->setDuration(150);
			anim->setStartValue(1.0f);
			anim->setEndValue(0.0f);

			vanish->addAnimation(anim);
		}

		m_transition->addAnimation(vanish);
	}

	// move and scale
	if (remainingNodes.size())
	{
		QParallelAnimationGroup* remain = new QParallelAnimationGroup();

		for (std::pair<QtGraphNode*, QtGraphNode*> p : remainingNodes)
		{
			QtGraphNode* newNode = p.first;
			QtGraphNode* oldNode = p.second;

			QPropertyAnimation* anim = new QPropertyAnimation(oldNode, "pos");
			anim->setDuration(300);
			anim->setStartValue(oldNode->pos());
			anim->setEndValue(newNode->pos());

			remain->addAnimation(anim);

			connect(anim, SIGNAL(finished()), newNode, SLOT(showNode()));
			connect(anim, SIGNAL(finished()), oldNode, SLOT(hideNode()));
			newNode->hide();

			anim = new QPropertyAnimation(oldNode, "size");
			anim->setDuration(300);
			anim->setStartValue(oldNode->size());
			anim->setEndValue(newNode->size());

			remain->addAnimation(anim);

			if (newNode->isAccessNode() && newNode->getSubNodes().size() == 0 && oldNode->getSubNodes().size() > 0)
			{
				dynamic_cast<QtGraphNodeAccess*>(oldNode)->hideLabel();
			}
		}

		m_transition->addAnimation(remain);
	}

	// fade in
	if (appearingNodes.size() || m_edges.size())
	{
		QParallelAnimationGroup* appear = new QParallelAnimationGroup();

		for (QtGraphNode* node : appearingNodes)
		{
			QPropertyAnimation* anim = new QPropertyAnimation(node, "opacity");
			anim->setDuration(300);
			anim->setStartValue(0.0f);
			anim->setEndValue(1.0f);

			appear->addAnimation(anim);

			connect(anim, SIGNAL(finished()), node, SLOT(blendIn()));
			node->blendOut();
		}

		for (std::shared_ptr<QtGraphEdge> edge : m_edges)
		{
			QPropertyAnimation* anim = new QPropertyAnimation(edge.get(), "opacity");
			anim->setDuration(150);
			anim->setStartValue(0.0f);
			anim->setEndValue(1.0f);

			appear->addAnimation(anim);

			edge->setOpacity(0.0f);
		}

		m_transition->addAnimation(appear);
	}

	connect(m_transition.get(), SIGNAL(finished()), this, SLOT(finishedTransition()));
	m_transition->start();
}
