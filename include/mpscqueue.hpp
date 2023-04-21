#include <atomic>
#include <optional>

namespace aiopp {
// Vyukov MPSC (wait-free multiple producers, single consumer) queue
// https://www.1024cores.net/home/lock-free-algorithms/queues/intrusive-mpsc-node-based-queue
template <typename T>
class MpscQueue {
public:
    MpscQueue()
        : stub_()
        , consumeEnd_(&stub_)
        , produceEnd_(&stub_)
    {
    }

    void produce(T&& value)
    {
        produce(new Node { std::move(value), nullptr });
    }

    std::optional<T> consume()
    {
        auto node = consumeEnd_.load();
        auto next = node->next.load();

        // If we are supposed to consume the stub, then the list is either empty (nullopt)
        // or this is the first time we consume, in which case we just move consumeEnd ahead.
        if (node == &stub_) {
            if (!next) {
                return std::nullopt;
            }
            consumeEnd_.store(next);
            node = next;
            next = node->next;
        }

        if (next) {
            consumeEnd_.store(next);
            return unpackNode(node);
        }

        // If we don't have a `next` element, `node` should be the last item in the list,
        // unless a new item was produced since we last loaded consumeEnd.
        // If there was, we need to try from the start (because there would be a `next`).
        // Instead of calling consume recursively (dangerous), we just bail and let the caller
        // retry.
        // I am fairly sure you could leave this check out completely and it would still work
        // correctly, but it would be less efficient.
        if (node != produceEnd_.load()) {
            return std::nullopt;
        }

        // Assuming the check above failed (and we got here), the state of the list should be:
        // stub -> node (consumeEnd, produceEnd) -> nullptr

        // Since we have no next item to make the new consumeEnd, we need to put stub_ into the
        // queue again.

        stub_.next.store(nullptr);
        produce(&stub_);

        // Now we have either attached stub to `node` or to another element other producer threads
        // might have added in the meantime.
        // In case we have finished attaching the other element to stub_, but the other producer
        // thread has not finished attaching `node` to the new element (i.e. it did not set
        // node->next yet), the below condition (`if (next)`) would be false.

        // Assuming one other producer thread the list would look like this (next != NULL):
        // node (consumeEnd) *(-> elem) -> stub (produceEnd)
        // or this (next is NULL):
        // node (consumeEnd) -X- elem -> stub (produceEnd)
        // The latter case is what Vyukov refers to saying that the consumer is blocking (see source
        // link).

        next = node->next.load();
        if (next) {
            consumeEnd_.store(next);
            return unpackNode(node);
        }

        // If the other thread has not managed to attach the new element to `node` yet, we have no
        // other choice but to wait for it to finish, so we return nullopt.

        return std::nullopt;
    }

private:
    struct Node {
        T value;
        // "next" in the order of consumption
        std::atomic<Node*> next;
    };

    static T unpackNode(Node* node)
    {
        auto value = std::move(node->value);
        delete node;
        return value;
    }

    void produce(Node* node)
    {
        auto prev = produceEnd_.exchange(node);
        prev->next.store(node);
    }

    // This is not an actual element of the queue, but simply a place to "park" consumeEnd, when
    // there is nothing to consume.
    // Sadly this makes default constructability for T a requirement.
    Node stub_;
    // Yes, screw "head" and "tail" and everyone doing whatever they please with those words.
    std::atomic<Node*> consumeEnd_;
    std::atomic<Node*> produceEnd_;
};
}
