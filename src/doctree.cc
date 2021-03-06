#include "doctree.h"
#include "editwindow.h"
#include "app.h"
#include <cassert>
using Node = DocumentTreeView::Node;
using callback_t = DocumentTreeView::callback_t;

const callback_t *DocumentTreeView::searchCallback {nullptr};

Node::Node(Node *parent, std::string_view p) :
    TNode(TPath::basename(p)),
    ptr(nullptr),
    parent(parent),
    data(std::string {p})
{
}

Node::Node(Node *parent, EditorWindow *w) :
    TNode(w->title),
    ptr(nullptr),
    parent(parent),
    data(w)
{
}

bool Node::hasEditor() const {
    return std::holds_alternative<EditorWindow *>(data);
}

EditorWindow* Node::getEditor() {
    if (auto **pw = std::get_if<EditorWindow *>(&data))
        return *pw;
    return nullptr;
}

void Node::setParent(Node *parent_) {
    if (parent != parent_) {
        remove();
        parent = parent_;
        if (parent)
            putLast(&parent->childList, this);
    }
}


void Node::remove()
{
    if (next)
        ((Node *) next)->ptr = ptr;
    if (ptr) {
        *ptr = next;
        ptr = nullptr;
    }
    if (parent && !parent->childList)
        parent->dispose();
}

void Node::dispose()
{
    assert(!childList);
    remove();
    delete this;
}

void DocumentTreeView::focused(int i)
{
    // Avoid reentrancy on focus:
    // focused() => w->focus() => app->setFocusedEditor() => focusEditor() => focused()
    if (!focusing) {
        focusing = true;
        TOutline::focused(i);
        if (auto *node = (Node *) getNode(i)) {
            if (auto *w = node->getEditor())
                w->focus();
        }
        focusing = false;
    }
}

void DocumentTreeView::addEditor(EditorWindow *w)
{
    Node *parent;
    TNode **list;
    if (w->file.empty()) {
        parent = nullptr;
        list = &root;
    } else {
        parent = getDirNode(TPath::dirname(w->file));
        list = &parent->childList;
    }
    putLast(list, new Node(parent, w));
    update();
    drawView();
}

void DocumentTreeView::focusEditor(EditorWindow *w)
{
    // Avoid the reentrant case (see focused()).
    if (focusing)
        return;
    int i;
    if (findFirst(hasEditor(w, &i)))
        focused(i);
    drawView();
}

void DocumentTreeView::removeEditor(EditorWindow *w)
{
    if (auto *f = (Node *) findFirst(hasEditor(w))) {
        f->dispose();
        update();
        drawView();
    };
}

void DocumentTreeView::focusNext()
{
    findFirst([this] (auto *node, auto pos) {
        if (((Node *) node)->hasEditor() && pos > foc) {
            focused(pos);
            drawView();
            return true;
        }
        return false;
    });
}

void DocumentTreeView::focusPrev()
{
    int prevPos = -1;
    findFirst([this, &prevPos] (auto *node, auto pos) {
        if (((Node *) node)->hasEditor()) {
            if (pos < foc)
                prevPos = pos;
            else if (prevPos >= 0) {
                focused(prevPos);
                drawView();
                return true;
            }
        }
        return false;
    });
}

Node* DocumentTreeView::getDirNode(std::string_view dirPath)
{
    // The list where the dir will be inserted.
    TNode **list {nullptr};
    Node *parent {nullptr};
    {
        auto parentPath = TPath::dirname(dirPath);
        if ((parent = (Node *) findFirst(hasPath(parentPath))))
            list = &parent->childList;
    }
    if (!list)
        list = &root;
    // The directory we are searching for.
    auto *dir = (Node *) findInList(list, [dirPath] (Node *node) {
        auto *ppath = std::get_if<std::string>(&node->data);
        return ppath && *ppath == dirPath;
    });
    if (!dir) {
        dir = new Node(parent, dirPath);
        // Place already existing subdirectories under this dir.
        findInList(&root, [dir, dirPath] (Node *node) {
            auto *ppath = std::get_if<std::string>(&node->data);
            if (ppath && TPath::dirname(*ppath) == TStringView(dirPath))
                node->setParent(dir);
            return false;
        });
        // Directories are put at the beginning of their list.
        putFirst(list, dir);
    }
    return dir;
}

TNode *DocumentTreeView::findFirst(const callback_t &cb)
{
    auto *tmp = searchCallback;
    searchCallback = &cb;
    auto *ret = firstThat(applyCallback);
    searchCallback = tmp;
    return ret;
}

Boolean DocumentTreeView::applyCallback(TOutlineViewer *, TNode *node, int, int position, long, ushort)
{
    return Boolean((*searchCallback)(node, position));
}

callback_t DocumentTreeView::hasEditor(const EditorWindow *w, int *pos)
{
    return [w, pos] (auto *node, auto position) {
        auto *w_ = ((Node *) node)->getEditor();
        if (w_ && w_ == w) {
            if (pos)
                *pos = position;
            return true;
        }
        return false;
    };
}

callback_t DocumentTreeView::hasPath(std::string_view path, int *pos)
{
    return [path, pos] (auto *node, auto position) {
        auto *ppath = std::get_if<std::string>(&((Node *) node)->data);
        if (ppath && *ppath == path) {
            if (pos)
                *pos = position;
            return true;
        }
        return false;
    };
}

DocumentTreeWindow::DocumentTreeWindow(const TRect &bounds, DocumentTreeWindow **ptr) :
    TWindowInit(&DocumentTreeWindow::initFrame),
    TWindow(bounds, "Open Editors", wnNoNumber),
    ptr(ptr)
{
    auto *hsb = standardScrollBar(sbHorizontal),
         *vsb = standardScrollBar(sbVertical);
    tree = new DocumentTreeView(getExtent().grow(-1, -1), hsb, vsb, nullptr);
    tree->growMode = gfGrowHiX | gfGrowHiY;
    insert(tree);
}

DocumentTreeWindow::~DocumentTreeWindow()
{
    if (ptr)
        *ptr = nullptr;
}

void DocumentTreeWindow::close()
{
    message(TurboApp::app, evCommand, cmToggleTree, 0);
}
