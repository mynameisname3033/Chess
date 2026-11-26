# Parallax Details

# 1\. Introduction

You've probably heard the term “chess engine” before — maybe from a YouTube video, an article, or the evaluation bar on a site like Lichess. I've always been curious how one actually works under the hood. How does it know whether a position is good? How does it decide on the “best move”? Engines like Stockfish are remarkably advanced, but I wanted to build my own from scratch, in C++, to genuinely understand every layer of the system rather than just the surface-level idea.

First, it helps to grasp the general architecture most traditional engines follow. The search itself is typically built around an algorithm called “negamax” — a streamlined variant of the classic “minimax” — but the overall pipeline looks roughly like this:

*Position → Move Generation → Searches → Evaluations → Best Move*

You'll notice I used the plural of “search” and “evaluation” here; you'll see why shortly. For now, let's talk about the backend.

# 2\. Internal Representations

## 2.1 Pieces and Moves

We need a way to represent full pieces (the piece type — 6 possibilities, and color — 2 possibilities) as well as moves (the from-square — 64 possibilities, the to-square — 64 possibilities, and additional flags — 11, in my case).

Encoding the pieces is straightforward. I used 3 bits for the piece type, since 2 bits would only allow for 4 distinct values and we need 6\. Then I added one more bit for color, which only needs to be 0 or 1 since there are just two colors. The whole thing fits comfortably into a single 8-bit integer, with a few bits to spare.

Moves are trickier to encode, mostly because of how many flags need to be packed in and how fiddly it is to unpack them individually. Let's start with the from-square and to-square. Each fits in 6 bits, since 2⁶ \= 64, which makes them easy to work with.

Next, the flags. Here's the full set I used, totaling 11:

QUIET \= 0,  
DOUBLE\_PAWN \= 1,  
EN\_PASSANT \= 2,

PROMO\_KNIGHT \= 4,  
PROMO\_BISHOP \= 5,  
PROMO\_ROOK \= 6,  
PROMO\_QUEEN \= 7  
   
CASTLE\_WHITE\_KINGSIDE \= 8,  
CASTLE\_WHITE\_QUEENSIDE \= 9,  
CASTLE\_BLACK\_KINGSIDE \= 10,  
CASTLE\_BLACK\_QUEENSIDE \= 11

I skipped the number 3 to make it easier to catch specific bits that correspond to a larger flag. For example, we can check if a move is a promotion if it has its 3rd bit from the right on, like this:

move & 0b0100

During my research, I came across the recommendation to also include a CAPTURE flag, but I didn't find it necessary — and implementing it would have meant introducing extra combined states like PROMO\_KNIGHT\_CAPTURE, which felt like unnecessary complexity. Since the board already maintains a square-indexed array (more on that in the next section), captures are trivially detectable without a dedicated flag. So I left it out, both for simplicity and to keep the encoding manageable.

And that's it — the whole move fits into a single 16-bit integer. With pieces and moves both squared away, let's move on to the board.

## 2.2 The Board

Intuitively, we need some way to represent the board itself. So what does a board actually need to store? You might assume it's just an 8x8 grid of squares, each holding a value that identifies which piece sits there — and you'd be right, as far as it goes. We could use a plain 64-length array. However:

1. *This is quite slow. Limiting ourselves to per-square lookups forces extra work whenever we need to query or manipulate the positions of a specific piece type.*

2. *A board needs to track far more state than piece placement alone.*

So we need something more. Conveniently, an 8x8 grid works out to exactly 64 squares — and if you're familiar with low-level programming, you'll recognize 64 bits as the width of a “long,” “unsigned long long,” or uint64\_t. I'll use the term “bitboard” for these integers going forward.

So that's it, right? We're done?

Not quite. Every bit in a bitboard is either a 1 or a 0, but we need to represent 12 distinct piece types (6 pieces × 2 colors) — a single bitboard can't hold that much information. Instead, we use 12 separate bitboards, one per full piece type, where each bit indicates whether that specific piece — say, the white knight — occupies the corresponding square. The bit positions map directly onto the squares of the board.

That said, I still keep a 64-length array of pieces alongside the bitboards — it turns out to be invaluable for quickly detecting captures and a handful of other lookups down the road. Since each entry only needs 4 of its 8 bits to encode a piece, the rest go to good use: I reserve 0xFF as a sentinel meaning “empty square,” which sits safely outside the range of any real piece encoding.

If that all made sense, the rest of the board struct is easy. We also keep: an “occupied” table holding the combined bitboards for each color; a hash (more on that shortly); a repetition stack of previous position hashes, used to detect threefold repetition; a halfmove clock for the 50-move rule; each side's king square; an en passant square marking where a pawn could legally capture en passant (-1 if none apply); an 8-bit field of castling rights (one bit per right, with 4 bits left unused); and finally, the side to move.

All told, my board struct comes out to 1232 bytes, the bulk of which is the repetition stack. If memory is tight, you can drop the repetition stack to save space and squeeze out a bit more performance — at the cost of no longer being able to detect threefold repetition.

To actually play a move, we unpack the from-square, to-square, and flags from the action, then update the board's internal members accordingly.

It's common practice to also implement an undo\_move function, but I chose not to. In my own testing, copying the entire board on every move turned out to be slightly faster — by about \~6% —  than writing and maintaining a function to manually reverse one — so I left it out.

Next, let's get into the actual search algorithm.

# 3\. Search

## 3.1 Understanding

As mentioned, the algorithm I used is called negamax — a variant of the better-known minimax, just considerably cleaner to implement. It works by recursively exploring candidate lines up to some depth, evaluating each one, and selecting whichever it judges most favorable.

To build intuition before diving into the technical details, let's walk through a simplified example: tic-tac-toe instead of chess. Say the board currently looks like this:

 *\| \|O*  
*O\|X\|*  
*X\|X\|O*

For a human, it's immediately obvious that the winning move is square 2, completing a vertical line for X. For a computer, though, this isn't obvious at all — it has to consider every possible move, and for each of those, every subsequent move, recursively, until each branch terminates in what's called a terminal node: an end state representing a tie, a win, or a loss.

So let's step through it. The only legal moves are on squares 1, 2, and 6\. Suppose we start by playing square 1\. The board now looks like this:

*X\| \|O*  
*O\|X\|*  
*X\|X\|O*

It's now O's turn, and we repeat the process. The only remaining moves are squares 2 and 6\. If O plays square 2 — I won't play out every detail — it's easy to see the game continues to a tie, with X forced onto square 6\. If O plays square 6 instead, the board looks like this:

*X\| \|O*  
*O\|X\|O*  
*X\|X\|O*

This is a terminal node — O wins outright. That node returns a winning score to its parent, which in turn passes a losing score up to its own parent — the grandparent node, i.e., our starting position — since scores get negated as they propagate upward (more on that shortly). So, from the perspective of the starting position, playing square 1 nets a loss.

With that branch exhausted, the computer “backtracks” automatically — one of the perks of recursion — returning to the starting position. Now we try the next candidate move, square 2\. The board looks like this:

 *\|X\|O*  
*O\|X\|*  
*X\|X\|O*

This time, no further recursion is even needed — it's already a terminal node, this time with X winning outright. We return a winning score immediately, which the parent node picks up as the best available move: X wins.

Here's a diagram of a different position to help visualize what's happening:

![][image1]  
To avoid confusion, note that this diagram depicts minimax, not negamax. It models two players explicitly — one maximizing a shared global score, the other minimizing it — which is why non-tie terminal nodes are hardcoded to \-10 for O and \+10 for X.

Negamax works differently: it negates the score at each recursive call — hence the name — which is valid because a position's score from one player's perspective is simply the negation of that same position's score from the opponent's perspective. This symmetry keeps the implementation considerably cleaner and eliminates a lot of edge-case handling.

The pseudocode for the recursive negamax function for tic-tac-toe looks like this:

int negamax(isX, depth)  
{  
    if (win)  
        return 10 \- depth  
    else if (loss)  
        return depth \- 10  
    else if (tie)  
        return 0  
   
    best\_score \= \-INF  
   
    for (move in moves)  
    {  
        make\_move(move, isX)  
        score \= \-negamax(\!isX, depth \+ 1\)  
        unmake\_move(move, isX)  
   
        if (score \> best\_score)  
            best\_score \= score  
    }  
   
    return best\_score  
}

Notice the depth parameter, used to offset the winning and losing scores — this is what lets the engine prefer faster wins and slower losses, a detail the diagram above doesn't capture. For instance, if there's a forced win one ply (depth) away and another two plies away, the root node will prefer the closer one, since it yields a higher score: 10 \- 1 \> 10 \- 2\.

We can then write a near-identical function for the root position that, instead of returning just the best score, also tracks and returns the best move.

For chess, the concept carries over directly. This is also where the “evaluation” you see on sites like Lichess actually comes from — it's simply the best score found at the root node.

There's a catch, though. In tic-tac-toe, this approach can compute a mathematically perfect strategy, thanks to how small its state space is: only about 5,478 possible positions. Chess is a different story entirely. With roughly 4.82 × 10⁴⁴ legal positions — for scale, there are an estimated 1.33 × 10⁵⁰ atoms on Earth — exhaustively searching the whole game tree is simply infeasible. So we need techniques that make the engine both faster (fewer nodes searched) and smarter (better nodes searched). Searching a winning capture, for instance, is generally far more productive than searching a random pawn push.

This also introduces a new kind of node worth naming: the leaf node. A terminal node marks a true end to the game — a win, loss, or tie. A leaf node just marks where the search itself stops, once it's gone as deep as time or depth allows. Every terminal node happens to be a leaf node, but not every leaf node is terminal — most of the time, we're simply cutting the search off mid-thought.

Finally, in chess, the overwhelming majority of nodes searched — easily 99%+ — are never terminal nodes; forced mates mostly show up only in endgames or the occasional fortunate middlegame. So we need a way to evaluate non-terminal positions too. That's where static evaluation comes in, which we'll cover shortly.

## 3.2 Quiescence

Before getting into quiescence, it helps to be precise about what static evaluation is actually for. The "static" in the name is the giveaway: it's meant to judge a position's underlying quality — material, structure, king safety, etc. — not to calculate forced sequences a move or two ahead. A queen that's about to be lost still looks, to a static eval, like a queen that's perfectly safe, because nothing about the position itself says otherwise. It may pick up faint traces of tactical patterns by accident, but spotting tactics was never its job — that's what the search is for. All of its "attention" should go toward positional judgment instead.

A "quiet" position, then, is one with no immediate tactics left to resolve — nothing hanging, nothing forced — which means the static eval can actually be trusted on it. That's exactly where the term "quiescence" comes from: a state of quiet.

Quiescence search is a narrower, specialized search that kicks in at each of negamax's leaf nodes, once the main search has run out of depth to give. Rather than considering every legal move the way negamax does, it restricts itself to captures and promotions — the moves most likely to still be resolving a material swing — plus, in my engine, a depth-limited allowance for checks, to catch forced sequences hiding just past the horizon. Captures, promotions, and checks aren't every tactical idea a position can hold, but they're responsible for the overwhelming majority of them, which makes this a remarkably cheap way to buy a lot of tactical safety.

## 3.3 Alpha-Beta Pruning

Here is our first — and probably the most complex — search optimization. Things will get a little more involved — it took me over a week to fully wrap my head around alpha-beta pruning, but I'll do my best to explain it clearly.

Suppose we've evaluated one move and found it scores \+5. Now we move on to a different candidate and discover that, in the resulting position, the opponent has a reply that forces the score down to \-2. Continuing to search that branch further is pointless — we already know it's worse than what we've already found.

This is exactly what alpha-beta pruning addresses. Alpha represents the best score the side to move has guaranteed so far; beta represents the best score the opponent is willing to allow before they'd simply avoid this branch altogether. Alpha updates as we iterate through moves, tracking our best guarantee; beta is inherited from the opponent's own alpha one ply up. In short, beta is just the best outcome the opponent has already secured somewhere else in the tree.

So whenever alpha meets or exceeds beta — meaning our best guaranteed score is already better than what the opponent would ever allow — we can prune the remaining branch entirely, since a rational opponent would simply steer away from it.

To pass alpha and beta down into child nodes, we swap and negate both values, turning our earlier tic-tac-toe call — negamax(player, alpha, beta, depth) — into:

int score \= \-negamax(\!isX, \-beta, \-alpha, depth \+ 1\)

We can push alpha-beta pruning even further by trying to guess which moves are likely best and searching those first. Establishing a tight, accurate alpha and beta earlier means the engine can start pruning sooner, which dramatically cuts down the total number of nodes it has to search.

There are several ways to estimate how promising a move is. Promotions are — 99 times out of 100 — simply the best move available, so those get tried early. After that, at least in my ordering, come winning captures: captures that still come out ahead once you play out the full sequence of exchanges on that square, not just the immediate trade but every recapture either side could make afterward. Last comes a mix of various "history" heuristics, which estimate how good a move is based on how well that same move performed in similar positions earlier in the search. There are several flavors of these histories, and they all work identically under the hood — the only real difference is what each one counts as a "similar" position. Countermove history, for instance, keys off whichever move the opponent just played; the plain history heuristic keys off just the move's own from-and-to squares. Next, intuitively, is losing captures, and these can even be entirely discarded if implemented properly in some engines. Finally — and we'll come back to this later — if a move was already flagged as the best one from a previous search of this exact position, we place it first of all, since that's usually the strongest candidate we have.

## 3.4 Iterative Deepening

One obvious problem left to solve is time management. As it stands, we'd have to hand negamax a fixed depth up front, and it would have to fully complete that depth no matter how long it takes. That's fairly crude. Wouldn't it be better to determine the search depth dynamically instead?

Iterative deepening solves exactly this. Rather than diving straight into one large fixed-depth search, we search depth 1, then depth 2, then depth 3, and so on, stopping whenever we run out of time. This might sound wasteful, since we're re-searching nodes we've already covered at shallower depths, but the next optimization takes care of that by letting us reuse results from positions we've already searched.

Beyond dynamic depth control, iterative deepening also improves move ordering for free. We can carry over the best move (or best few moves) found at the root from the previous, shallower iteration, and search those first next time. As we've already seen, better move ordering dramatically improves pruning efficiency, since it's the first branch explored that does the most work.

## 3.5 The Transposition Table

A relatively simple but highly effective optimization — arguably the most impactful one after alpha-beta pruning itself — is the transposition table. The same position tends to recur many times over the course of a search (through different move orders reaching the same board state), so we can cache the results of a search and reuse them if we encounter that position again. We do, however, need to stay memory-efficient and careful about correctness.

This is where the 64-bit hash each board carries comes in handy. We generate a unique-enough key for every position and use it to index directly into the transposition table.

Because of iterative deepening, the same position can end up stored at different remaining depths across different calls. To judge how trustworthy a stored result is, we look at its depth\_remaining, since that tells us how far ahead it was actually searched. You might wonder how the same position ends up searched to different depths at all — again, this comes down to iterative deepening: a depth-15 search that reaches some position at ply 4 will have searched it more thoroughly (and thus more accurately) than a depth-6 search reaching that same position.

So when storing a new entry, we need some policy for deciding which one to actually keep if the slot's already occupied. The simplest approach: compare depth\_remaining values and keep whichever is deeper. In practice, production engines tend to layer on more nuance — bucketing several entries per index and factoring in how recently each one was accessed — but comparing depth\_remaining is a solid foundation to start from.

## 3.6 Pruning Techniques

Next, we'll cover a handful of techniques for cutting unpromising nodes out of the search entirely. Before diving in, though, it's worth flagging that these — along with much of the next chapter — only pay off if the rest of the engine is already solid, especially move ordering. Bolt them onto a shaky foundation and they can easily make things worse, not better.

First on the list — and probably the most effective — is null-move pruning. It sounds almost illegal at first: for any node that passes a certain set of conditions (more on those in a moment), we flip the side to move and search on to a reduced depth, essentially asking "what if I just passed my turn here?" If that search's result still comes back greater than or equal to beta, it tells us something striking: the position is so good for us that even gifting our opponent a free move doesn't let them claw back an advantage. Just like in alpha-beta pruning, we can prune the node outright, since a rational opponent would never let us reach a position this favorable in the first place.

Before trusting that result, though, we run one more check — a verification search using a real move instead of a null one, searched slightly deeper than the null-move search was. This guards against zugzwang, positions where the side to move would genuinely be better off passing than playing any legal move, which would otherwise trick the null-move search into a false cutoff. If the verification search also comes back at or above beta, we return beta.

The conditions I settled on: the side to move can't be in check, depth\_remaining has to clear some minimum so the reduced-depth search is still meaningful, the position still has to have non-pawn material on the board — pure pawn endgames are exactly where zugzwang tends to live, so null-move pruning is disabled there entirely rather than trusting the verification search to catch every case — and the node can't be part of the line we currently believe is best (a "PV node"). That last condition applies to everything else in this chapter too: we reserve full-strength search for moves we already trust, and only lean on these shortcuts elsewhere.

Next on the list is reverse futility pruning — essentially the mirror image of the futility pruning we're about to cover, applied one level up. Reverse futility pruning works by comparing our static evaluation, minus a safety margin that grows with depth\_remaining, against beta. If even after subtracting that margin the position still looks at least as good as beta, we treat the node as already won and return the static evaluation immediately — no need to search a single move.

The margin exists because static evaluation gets less trustworthy the further a position is from the actual leaves of the search; at greater depth\_remaining, there's more room for something the eval can't see to change the picture, so the safety margin scales up alongside it. Near the horizon, where depth\_remaining is small, we only need a small cushion before we're willing to trust the static eval outright.

It's worth noting how this differs from null-move pruning, even though both lean on a similarly favorable position: null-move pruning still runs a reduced search — and a verification search on top of that — before committing to a cutoff, since it's making a genuine claim about what happens several moves out. Reverse futility pruning skips the search step entirely and trusts the static evaluation directly, which only holds up because the margin does the work of hedging against how wrong that evaluation might be.

Finally, there's futility pruning proper, which takes the same core idea — static evaluation, padded with a margin, standing in for "the best this position could still become" — and applies it move by move instead of to the whole node at once.

Rather than deciding whether to search anything at all, futility pruning decides whether one specific quiet move is even worth trying. For each quiet move — once we're already past the first move at a node, and assuming it isn't a killer move — we compute a threshold: static evaluation plus a margin that again scales with depth\_remaining. If that threshold still falls short of alpha, then even the most optimistic estimate for the position after this move says it won't raise our score above what we've already guaranteed, so we skip the move without searching it. The first move at a node and any killer moves are exempt from this — the first move is usually the transposition-table move or our top-ordered candidate, and killers have already proven themselves as cutoffs elsewhere, so skipping either would risk throwing away a genuinely good move on the strength of a shortcut.

This only makes sense for quiet moves — ones that aren't captures, promotions, or checks — since those are exactly the moves unlikely to shift the evaluation by much in one step. And like reverse futility pruning, it only applies at non-PV nodes, positions where we aren't in check, and shallow enough depth\_remaining that a static evaluation plus a fixed margin is still a fair stand-in for the ceiling on how good this move could get.

And last but not least, late-move pruning. This one leans heavily on the quality of your move ordering, since it works by simply skipping moves that show up too late in an already-ordered move list — trusting that if ordering has done its job, a move sitting that far down the list has little chance of being the best one anyway.

Modern engines calculate that cutoff dynamically rather than using a fixed number. Mine looks like this:

index \= 3 \+ depth\_remaining \* depth\_remaining

I square depth\_remaining instead of scaling it linearly so the threshold grows sharply as more depth remains — strictly right at the horizon, but opening up quickly as depth\_remaining increases. That matches how lopsided a well-ordered move list tends to be: once ordering is doing its job, moves tend to split cleanly into "promising" and "hopeless" rather than fading gradually from one to the other, so a cutoff that reacts sharply to depth fits that pattern better than one that scales evenly would. The \+3 is just a floor, guaranteeing we always fully search a small handful of moves no matter how small depth\_remaining gets.

Like futility pruning, this only applies to quiet moves, exempts the first move and any killers for the same reasons as before, and is restricted to non-PV nodes. It's also worth being deliberate about depth here specifically — in my tuning, it only ever fires with a ply or two of depth remaining. Any earlier in the search than that (higher depth\_remaining, further from the leaves), and you risk pruning away a move that genuinely mattered; this technique is meant to clean up the tail end of a search, not shape the front of it.

## 3.7 Reduction Techniques

If pruning is about deciding a move isn't worth searching at all, reductions are the more cautious sibling: instead of skipping a move outright, we still search it — just not as deeply as we normally would. It's a bet: most moves this far down an already-ordered list turn out to be exactly as unremarkable as their position in the list suggests, so we spend less effort confirming that. If a reduced search unexpectedly disagrees, we simply re-search it properly. This is usually called late-move reductions, though sticking with "action" instead of "move," I called it LAR in my code.

The baseline reduction comes from a table, precomputed once at startup from a fairly standard formula:

reduction \= 1 \+ log(depth\_remaining) \* log(i) / 7

Using logarithms of the move index and depth, rather than the raw numbers themselves, keeps the reduction from growing too aggressively: the difference between the 4th and 8th move in the list matters a lot more than the difference between the 40th and 44th, and the same goes for depth. This table only actually gets consulted for moves that are quiet, aren't killers, sit past a minimum index, and occur at a depth\_remaining past its own minimum — moves too early in the list, too shallow in the search, tactically loud, or already flagged by move ordering as promising don't get touched by this baseline at all.

From there, the reduction gets adjusted using exactly the same history tables built for move ordering — a nice bit of reuse, since a table built to help order moves in the first place turns out to be just as useful for deciding how seriously to take a move once it's actually being searched. The plain history heuristic score for this from-and-to pair gets divided down and subtracted from the reduction, so a move with a strongly positive history nudges the reduction smaller (closer to full depth), while a poor history nudges it larger. On top of that gentle, proportional adjustment sits a coarser one: if the history score is clearly bad, the reduction gets bumped up by a full extra ply outright, and if it's clearly good, knocked down by a full ply — killers are exempted from the "clearly bad" penalty, since a move that already caused a cutoff elsewhere shouldn't be punished on history alone.

Continuation history gets folded in the same way, just from a different signal: rather than "how has this from-and-to move performed historically," it asks "how has this piece landed on this square performed, given what was played one or two plies before it." Same proportional term, same clearly-good/clearly-bad threshold nudge, same exemption for killers.

There's a small subtlety worth calling out here: after applying both of those, the code separately tracks exactly how much history and continuation, combined, shifted the reduction away from the table's baseline value. If that combined shift is large and negative — meaning the history evidence is overwhelmingly in the move's favor — it's treated as more than just "don't reduce this one." Past a certain threshold, the move is allowed to search deeper than normal, not merely at full depth: a genuine extension, capped at one extra ply. Reductions and extensions turn out to be two ends of the same lever, and strong enough history evidence is what tips a move from one side to the other.

Captures get their own version of this, but only real ones — promotions are excluded, since those are valuable enough on their own not to need a history check. The signal here is capture history, indexed by the piece doing the capturing, the square it lands on, and the piece it actually captures, which lets the engine learn patterns like "this particular trade tends to work out" independently of the from-and-to squares alone. It gets the same proportional-plus-threshold treatment as history and continuation. Worth noting: this is the only adjustment that applies to captures at all — the baseline table reduction earlier only ever fires for quiet moves, so a capture starts from a reduction of zero and only moves from there based on its own capture history.

Two smaller adjustments round things out. Moves in a PV node — one being searched with a genuinely wide window because we still believe it might contain the best line — get a flat reduction discount, since we're less willing to gamble with a shallow search on a line we already think matters. And any move that gives check gets reduced one ply less than it otherwise would, since a checking move forces a response from the opponent and can't be waved off the way an ordinary quiet move can — the history tables have no way of knowing that on their own.

All of these adjustments accumulate into a single number, which then gets clamped before use: it can never reduce more than new\_depth would allow (there's no such thing as searching to negative depth), and it can't go below a minimum — ordinarily zero, but as low as negative one when that history-extension condition fires. Whatever survives the clamp gets subtracted from new\_depth to get the actual depth we search this move to.

One last piece is worth understanding, since it's what makes reductions safe to use at all: the reduced search isn't run with the real alpha-beta window, but a deliberately narrow one — just enough to ask "does this beat alpha, or not," rather than "how much does this beat alpha by." That's cheaper to search, and it's all we actually need for most moves, since most of them won't beat alpha anyway. But if a move does slip through and come back above alpha despite the reduction, we don't trust that result outright — we re-search it, at the greater of the original depth or the reduced depth, with the real window this time, before deciding whether it's actually as good as it looked.

# 4\. Evaluation

## 4.1 Understanding a Neural Network

If you're already comfortable with neural networks, feel free to skip ahead. If not, I'd recommend sticking around. I won't go deep into the mechanics of training one, but I'll cover the general idea of how a neural network functions, along with its capabilities and limitations.

A neural network is, at its core, a foundational building block of modern AI — ChatGPT, Claude, and nearly every contemporary AI system rely on some variant of one. For a chess engine, though, evaluation speed is critical, so ours needs to stay small and tightly optimized. Before getting into that, let's cover how a neural network actually works.

Here's the general structure of a neural network:

![][image2]  
This diagram shows what I'll refer to as a 2×4×4×1 network. The input layer has 2 neurons, the two hidden layers each have 4, and the output layer has 1\. Each circle represents a neuron, each connecting line represents a weight, and — not pictured here — every neuron also carries a bias term. I'll use this terminology throughout.

Say this network is trying to estimate a house's price based on its age and square footage. Those two values feed into the two input neurons. We then propagate them forward: each input gets multiplied by every weight it connects to, producing eight products total — two feeding into each of the four neurons in the first hidden layer. For each neuron, we sum its incoming products and add its bias.

To make this concrete, here's the formula for the output of the first neuron in the first hidden layer. Let x1 and x2 be the two inputs, w1 and w2 be the weights connecting each input neuron to this particular hidden neuron, and b be this neuron's bias. Then we can say this about its output o:

$o=x1w1+x2w2+b$

We repeat this process — forward propagation — layer by layer until we reach the output layer, which gives us our final estimate for the house's price. Crucially, the parameters (weights and biases) are tunable: through training, we can adjust them mathematically until the model reliably estimates house prices — or, more generally, whatever problem we're trying to solve.

There's plenty about neural networks I haven't touched on here. For a more thorough and intuitive treatment, I'd strongly recommend 3Blue1Brown's series on the topic: [3Blue1Brown – Neural Networks](https://www.youtube.com/watch?v=aircAruvnKk&list=PLZHQObOWTQDNU6R1_67000Dx_ZCJB-3pi)

## 4.2 The NNUE

The same idea applies directly to chess: feed in some representation of the board, pass it through a handful of layers, and get back an evaluation.

But what kind of representation should we use? The most naive representation would be the raw bitboards for all 12 piece types concatenated together — 768 total inputs (12 × 64), each a simple 1 or 0, as this has a couple problems:

1. *768 inputs is a fair amount, and worse, it's static — every input has to be recomputed from scratch each time the network runs.*

2. *The inputs are unrefined. Later layers would need to be large and “smart” enough to extract meaningful structure from raw piece placement, or the network's evaluations would suffer.*

Both problems have a fairly elegant fix: embeddings. Think of an embedding as simply an n-length vector. In modern NNUE architectures, embeddings are typically sized to half the input layer, to account for the two perspectives (White's and Black's) that chess evaluation needs.

There are several variants of the NNUE architecture, such as HalfKA and HalfKP. I went with HalfKA, since it's the more modern approach and what Stockfish itself uses. Here, we maintain a large table of embeddings, each identified by an index. For every piece on the board, we compute its corresponding index, find the right embedding from the table, and add the vector into a running accumulator — quite literally summing them all together. Intuitively, this “combines” the influence of every piece on the board into a single representation the network can interpret more effectively, yielding a more accurate evaluation.

In HalfKA, each embedding is indexed by three things: the relevant perspective's king square, the piece type and color, and the square that the piece occupies. King-relative indexing exists specifically to teach the network about king safety — a position's evaluation can swing wildly depending on where the king sits, and it is, after all, the most important piece on the board. This does inflate the embedding count considerably, though: we'd go from (64 × 11\) up to (64 × 64 × 11). To rein that in, we mirror the king's position across the board's central file — files e through h collapse onto a through d — which halves the embedding count and helps the network generalize better. This mirroring is valid because a king on one file is functionally equivalent to a king on its mirror image.

I initially used an embedding size of 256, giving a (256 × 2)×32×32×1 network, but later increased it to 384 after seeing how large Stockfish's own layers were — so I figured I'd follow suit.

This embedding-based structure also enables incremental updates. Rather than rebuilding the accumulator from scratch every time we need an evaluation, we can update it incrementally based on whichever pieces just moved. If a pawn moves from a2 to a3, for instance, we simply subtract the embedding for “pawn on a2” and add the embedding for “pawn on a3” — relative to wherever the king happens to be — which makes the NNUE remarkably fast in practice.

## 4.3 SIMD Optimizations (Vectorization)

Running a 768×32×32×1 network plus embedding vector additions potentially millions of times over the course of a search is, understandably, expensive. Fortunately, there's a straightforward way to speed this up without sacrificing any accuracy: SIMD (AVX) vectorization.

Most modern CPUs include AVX registers capable of performing arithmetic on multiple values simultaneously — exactly what we need here. Forward propagation is a highly repetitive operation by nature, which makes it an ideal target for this kind of optimization.

Instead of processing neurons one at a time, we can feed several into each layer's propagation simultaneously. Exactly how many depends on the AVX generation in use (AVX2 vs. AVX-512) and the numeric type involved (floating point vs. integer — another optimization covered shortly). With floating point and AVX2, I was able to process 8 input neurons per instruction, which — conceptually — makes that stage of the NNUE 8x faster. One small consequence: layer sizes need to be a multiple of however many neurons you process per instruction — 8, in my case.

For example, the normal propagation of input→output between two layers can be written in pseudocode like this:

for (out\_neuron in out\_neurons.length)  
{  
    sum \= 0  
   
    for (in\_neuron \= 0, in\_neuron \< in\_neurons.length, in\_neuron \+= 8\)  
    {  
        sum \= avx\_fmadd(in\_neurons\[in\_neuron\],  
                         weights\[out\_neuron\]\[in\_neuron\], sum)  
    }  
   
    sum \= hsum(sum)  
    sum \+= bias  
    next\_layer\[out\_neuron\] \= sum  
}

## 4.4 Quantization

Even with incremental updates and embeddings, the network still isn't blazing fast on its own — and raw nodes-per-second (NPS) is essential for a chess engine. Since the unquantized network — at least in my engine — ended up dominating total search time, quantization became necessary.

Quantization essentially rescales floating-point values onto an integer scale, replacing floats with integers throughout the network. Typically, the accumulator is stored as 16-bit integers and biases as 32-bit to guard against overflow, while weights get compressed down to just 8 bits. Because integers are smaller, we can pack more of them into each SIMD instruction — conceptually up to a 16x speedup for the layers that use it.

You might assume that squeezing floating-point numbers down into 8-bit integers would devastate evaluation precision, but it matters far less than you'd expect. An 8-bit integer gives roughly 0.0078125 of resolution across the range \[-1, 1\] — more than enough for an accurate evaluation. And even where precision does dip slightly, the speed gained from quantization more than makes up for it. If that's still not convincing, consider that a larger network can absorb the small precision loss while simultaneously “spending” the extra NPS quantization buys you — a genuine win-win. This tradeoff is exactly what pushed me from an embedding size of 256 up to 384\.
