/*
 * Copyright (C) 2013-2016 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "AnchorPositionEvaluator.h"
#include "PropertyCascade.h"
#include "SelectorChecker.h"
#include "SelectorMatchingState.h"
#include "StyleChange.h"
#include "StyleUpdate.h"
#include "Styleable.h"
#include "TreeResolutionState.h"
#include <wtf/Function.h>
#include <wtf/Ref.h>

namespace WebCore {

class Document;
class Element;
class Node;
class RenderStyle;
class ShadowRoot;

namespace Style {

class Resolver;
struct BuilderPositionTryFallback;
struct MatchResult;
struct PositionTryFallback;
struct PseudoElementIdentifier;
struct ResolutionContext;
struct ResolvedStyle;

enum class IsInDisplayNoneTree : bool { No, Yes };

DECLARE_ALLOCATOR_WITH_HEAP_IDENTIFIER(TreeResolverScope);
class TreeResolver {
public:
    TreeResolver(Document&, std::unique_ptr<Update> = { });
    ~TreeResolver();

    std::unique_ptr<Update> resolve();

    bool needsInterleavedLayout() const { return m_needsInterleavedLayout; }

private:
    enum class ResolutionType : uint8_t { RebuildUsingExisting, AnimationOnly, FastPathInherit, FullWithMatchResultCache, Full };
    ResolvedStyle styleForStyleable(const Styleable&, ResolutionType, const ResolutionContext&, const RenderStyle* existingStyle);

    void resolveComposedTree();

    const RenderStyle* existingStyle(const Element&);

    enum class LayoutInterleavingAction : uint8_t { None, SkipDescendants };
    enum class DescendantsToResolve : uint8_t { None, RebuildAllUsingExisting, ChildrenWithExplicitInherit, Children, All };

    LayoutInterleavingAction updateStateForQueryContainer(Element&, const RenderStyle*, OptionSet<Change>&, DescendantsToResolve&);

    std::pair<ElementUpdate, DescendantsToResolve> resolveElement(Element&, const RenderStyle* existingStyle, ResolutionType);

    ElementUpdate createAnimatedElementUpdate(ResolvedStyle&&, const Styleable&, OptionSet<Change>, const ResolutionContext&, IsInDisplayNoneTree = IsInDisplayNoneTree::No);
    std::unique_ptr<RenderStyle> resolveStartingStyle(const ResolvedStyle&, const Styleable&, const ResolutionContext&);
    std::unique_ptr<RenderStyle> resolveAfterChangeStyleForNonAnimated(const ResolvedStyle&, const Styleable&, const ResolutionContext&);
    std::unique_ptr<RenderStyle> resolveAgainInDifferentContext(const ResolvedStyle&, const Styleable&, const RenderStyle& parentStyle,  OptionSet<PropertyCascade::PropertyType>, std::optional<BuilderPositionTryFallback>&&, const ResolutionContext&);
    const RenderStyle& parentAfterChangeStyle(const Styleable&, const ResolutionContext&) const;

    UncheckedKeyHashSet<AnimatableCSSProperty> applyCascadeAfterAnimation(RenderStyle&, const UncheckedKeyHashSet<AnimatableCSSProperty>&, bool isTransition, const MatchResult&, const Element&, const ResolutionContext&);

    std::optional<ElementUpdate> resolvePseudoElement(Element&, const PseudoElementIdentifier&, const ElementUpdate&, IsInDisplayNoneTree);
    std::optional<ElementUpdate> resolveAncestorPseudoElement(Element&, const PseudoElementIdentifier&, const ElementUpdate&);
    std::optional<ResolvedStyle> resolveAncestorFirstLinePseudoElement(Element&, const ElementUpdate&);
    std::optional<ResolvedStyle> resolveAncestorFirstLetterPseudoElement(Element&, const ElementUpdate&, ResolutionContext&);

    struct Scope : RefCounted<Scope> {
        WTF_MAKE_STRUCT_FAST_ALLOCATED_WITH_HEAP_IDENTIFIER(TreeResolverScope);
        Ref<Resolver> resolver;
        SelectorMatchingState selectorMatchingState;
        RefPtr<ShadowRoot> shadowRoot;
        RefPtr<Scope> enclosingScope;

        Scope(Document&, Update&);
        Scope(ShadowRoot&, Scope& enclosingScope);
        ~Scope();
    };

    struct Parent {
        Element* element;
        const RenderStyle& style;
        OptionSet<Change> changes;
        DescendantsToResolve descendantsToResolve { DescendantsToResolve::None };
        bool didPushScope { false };
        bool resolvedFirstLineAndLetterChild { false };
        bool needsUpdateQueryContainerDependentStyle { false };
        IsInDisplayNoneTree isInDisplayNoneTree { IsInDisplayNoneTree::No };

        Parent(Document&);
        Parent(Element&, const RenderStyle&, OptionSet<Change>, DescendantsToResolve, IsInDisplayNoneTree);
    };

    Scope& scope() { return m_scopeStack.last(); }
    const Scope& scope() const { return m_scopeStack.last(); }

    Parent& parent() { return m_parentStack.last(); }
    const Parent& parent() const { return m_parentStack.last(); }

    void pushScope(ShadowRoot&);
    void pushEnclosingScope();
    void popScope();

    void pushParent(Element&, const RenderStyle&, OptionSet<Change>, DescendantsToResolve, IsInDisplayNoneTree);
    void popParent();
    void popParentsToDepth(unsigned depth);

    DescendantsToResolve computeDescendantsToResolve(const ElementUpdate&, const RenderStyle* existingStyle, Validity) const;
    static std::optional<ResolutionType> determineResolutionType(const Element&, const RenderStyle*, DescendantsToResolve, OptionSet<Change> parentChange);
    static void resetDescendantStyleRelations(Element&, DescendantsToResolve);

    ResolutionContext makeResolutionContext();
    ResolutionContext makeResolutionContextForPseudoElement(const ElementUpdate&, const PseudoElementIdentifier&);
    std::optional<ResolutionContext> makeResolutionContextForInheritedFirstLine(const ElementUpdate&, const RenderStyle& inheritStyle);
    const Parent* boxGeneratingParent() const;
    const RenderStyle* parentBoxStyle() const;
    const RenderStyle* parentBoxStyleForPseudoElement(const ElementUpdate&) const;

    LayoutInterleavingAction updateAnchorPositioningState(Element&, const RenderStyle*, OptionSet<Change>);

    void generatePositionOptionsIfNeeded(const ResolvedStyle&, const Styleable&, const ResolutionContext&);
    std::unique_ptr<RenderStyle> generatePositionOption(const PositionTryFallback&, const ResolvedStyle&, const Styleable&, const ResolutionContext&);
    struct PositionOptions;
    void sortPositionOptionsIfNeeded(PositionOptions&, const Styleable&);
    std::optional<ResolvedStyle> tryChoosePositionOption(const Styleable&, const RenderStyle* existingStyle);

    // This returns the style that was in effect (applied to the render tree) before we started the style resolution.
    // Layout interleaving may cause different styles to be applied during the style resolution.
    const RenderStyle* beforeResolutionStyle(const Element&, std::optional<PseudoElementIdentifier>);
    void saveBeforeResolutionStyleForInterleaving(const Element&);

    bool hasUnresolvedAnchorPosition(const Element&) const;

    struct QueryContainerState {
        OptionSet<Change> changes;
        DescendantsToResolve descendantsToResolve { DescendantsToResolve::None };
        bool invalidated { false };
    };

    CheckedRef<Document> m_document;
    std::unique_ptr<RenderStyle> m_documentElementStyle;

    Vector<Ref<Scope>, 4> m_scopeStack;
    Vector<Parent, 32> m_parentStack;
    bool m_didSeePendingStylesheet { false };

    UncheckedKeyHashMap<Ref<Element>, std::optional<QueryContainerState>> m_queryContainerStates;
    bool m_needsInterleavedLayout { false };

    // This state gets passes to the style builder and holds state for a single tree resolution, including over any interleaving.
    TreeResolutionState m_treeResolutionState;
    HashMap<Ref<const Element>, std::unique_ptr<RenderStyle>> m_savedBeforeResolutionStylesForInterleaving;

    struct PositionOptions {
        std::unique_ptr<RenderStyle> originalStyle;
        Vector<std::unique_ptr<RenderStyle>> optionStyles { };
        size_t index { 0 };
        bool sorted { false };
        bool chosen { false };
    };
    HashMap<Ref<Element>, PositionOptions> m_positionOptions;

    std::unique_ptr<Update> m_update;
};

// Integrate with the HTML5 event loop instead, see EventLoop.cpp and consumers.
void deprecatedQueuePostResolutionCallback(Function<void()>&&);
bool postResolutionCallbacksAreSuspended();

class PostResolutionCallbackDisabler {
public:
    enum class DrainCallbacks : bool { No, Yes };
    explicit PostResolutionCallbackDisabler(Document&, DrainCallbacks = DrainCallbacks::Yes);
    ~PostResolutionCallbackDisabler();
private:
    DrainCallbacks m_drainCallbacks;
};

}

}
