#include "RoutingTableService.h"
#include "NetkeyDistributionService.h"
#include "../core/LoraMesher.h"

size_t RoutingTableService::routingTableSize() {
    return routingTableList->getLength();
}

RouteNode* RoutingTableService::findNode(uint16_t address) {
    routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            if (node->networkNode.address == address) {
                routingTableList->releaseInUse();
                return node;
            }

        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();
    return nullptr;
}

RouteNode* RoutingTableService::getBestNodeByRole(uint8_t role) {
    RouteNode* bestNode = nullptr;

    routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            if ((node->networkNode.role & role) == role &&
                (bestNode == nullptr || node->networkNode.metric < bestNode->networkNode.metric)) {
                bestNode = node;
            }

        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();
    return bestNode;
}

bool RoutingTableService::hasAddressRoutingTable(uint16_t address) {
    RouteNode* node = findNode(address);
    return node != nullptr;
}

uint16_t RoutingTableService::getNextHop(uint16_t dst) {
    RouteNode* node = findNode(dst);

    if (node == nullptr)
        return 0;

    return node->via;
}

uint8_t RoutingTableService::getNumberOfHops(uint16_t address) {
    RouteNode* node = findNode(address);

    if (node == nullptr)
        return 0;

    return node->networkNode.metric;
}

void RoutingTableService::processRoute(RoutePacket* p, int8_t receivedSNR) {
    if ((p->packetSize - sizeof(RoutePacket)) % sizeof(NetworkNode) != 0) {
        ESP_LOGE(LM_TAG, "Invalid route packet size");
        return;
    }
    
    // **PHASE 2A: CONSERVATIVE NETWORK ID FILTERING**
    // If we have a non-zero Local Network ID and packet has different non-zero Network ID -> reject
    // Treat 0x0000 as "unset" (do not use as a valid local network id for filtering)
    if (NetkeyDistributionService::getLocalNetworkId() != 0 &&
        p->networkId != 0 && 
        p->networkId != NetkeyDistributionService::getLocalNetworkId()) {
        ESP_LOGW(LM_TAG, "Rejected Hello from different network - Local: 0x%04X, Received: 0x%04X", 
                 NetkeyDistributionService::getLocalNetworkId(), p->networkId);
        return;
    }

    size_t numNodes = p->getNetworkNodesSize();
    ESP_LOGI(LM_TAG, "Route packet from %X with size %d, Network ID: 0x%04X", p->src, numNodes, p->networkId);

    NetworkNode* receivedNode = new NetworkNode(p->src, 1, p->nodeRole);
    processRoute(p->src, receivedNode);
    delete receivedNode;

    resetReceiveSNRRoutePacket(p->src, receivedSNR);

    for (size_t i = 0; i < numNodes; i++) {
        NetworkNode* node = &p->networkNodes[i];
        node->metric++;
        processRoute(p->src, node);
    }

    printRoutingTable();
}

void RoutingTableService::resetReceiveSNRRoutePacket(uint16_t src, int8_t receivedSNR) {
    RouteNode* rNode = findNode(src);
    if (rNode == nullptr)
        return;

    ESP_LOGI(LM_TAG, "Reset Receive SNR from %X: %d", src, receivedSNR);

    rNode->receivedSNR = receivedSNR;
}

void RoutingTableService::processRoute(uint16_t via, NetworkNode* node) {
    if (node->address != WiFiService::getLocalAddress()) {

        RouteNode* rNode = findNode(node->address);
        //If nullptr the node is not inside the routing table, then add it
        if (rNode == nullptr) {
            addNodeToRoutingTable(node, via);
            return;
        }

        //Update the metric and restart timeout if needed
        if (node->metric < rNode->networkNode.metric) {
            rNode->networkNode.metric = node->metric;
            rNode->via = via;
            resetTimeoutRoutingNode(rNode);
            ESP_LOGI(LM_TAG, "Found better route for %X via %X metric %d", node->address, via, node->metric);
        }
        else if (node->metric == rNode->networkNode.metric) {
            //Reset the timeout, only when the metric is the same as the actual route.
            resetTimeoutRoutingNode(rNode);
        }

        // Update the Role only if the node that sent the packet is the next hop
        if (getNextHop(node->address) == via && node->role != rNode->networkNode.role) {
            ESP_LOGI(LM_TAG, "Updating role of %X to %d", node->address, node->role);
            rNode->networkNode.role = node->role;
        }
    }
}

void RoutingTableService::addNodeToRoutingTable(NetworkNode* node, uint16_t via) {
    if (routingTableList->getLength() >= RTMAXSIZE) {
        ESP_LOGW(LM_TAG, "Routing table max size reached, not adding route and deleting it");
        return;
    }

    if (calculateMaximumMetricOfRoutingTable() < node->metric) {
        ESP_LOGW(LM_TAG, "Trying to add a route with a metric higher than the maximum of the routing table, not adding route and deleting it");
        return;
    }

    RouteNode* rNode = new RouteNode(node->address, node->metric, node->role, via);

    //Reset the timeout of the node
    resetTimeoutRoutingNode(rNode);

    routingTableList->setInUse();

    routingTableList->Append(rNode);

    routingTableList->releaseInUse();

    ESP_LOGI(LM_TAG, "New route added: %X via %X metric %d, role %d", node->address, via, node->metric, node->role);
    
    // Trigger callback when new node is added to routing table (if not suspended)
    if (onRoutingTableChanged != nullptr && !callbackSuspended) {
        ESP_LOGI(LM_TAG, "New node added - triggering routing table save callback");
        onRoutingTableChanged();
    } else if (callbackSuspended) {
        ESP_LOGD(LM_TAG, "Callback suspended - skipping save");
    }
}

NetworkNode* RoutingTableService::getAllNetworkNodes() {
    routingTableList->setInUse();

    int routingSize = routingTableSize();

    // If the routing table is empty return nullptr
    if (routingSize == 0) {
        routingTableList->releaseInUse();
        return nullptr;
    }

    NetworkNode* payload = new NetworkNode[routingSize];

    if (routingTableList->moveToStart()) {
        for (int i = 0; i < routingSize; i++) {
            RouteNode* currentNode = routingTableList->getCurrent();
            payload[i] = currentNode->networkNode;

            if (!routingTableList->next())
                break;
        }
    }

    routingTableList->releaseInUse();

    return payload;
}

void RoutingTableService::resetTimeoutRoutingNode(RouteNode* node) {
    // Dynamic timeout based on current Hello Mode
    // Timeout = Current Hello Interval × TIMEOUT_MULTIPLIER
    // This allows nodes to miss TIMEOUT_MULTIPLIER consecutive hellos before being removed
    uint16_t currentHelloInterval = LoraMesher::getInstance().getCurrentHelloDelay();
    uint32_t dynamicTimeout = currentHelloInterval * TIMEOUT_MULTIPLIER;
    
    node->timeout = millis() + dynamicTimeout * 1000;
    
    ESP_LOGD(LM_TAG, "Reset timeout for node 0x%04X: %u seconds (Hello interval: %us × %d)",
             node->networkNode.address, dynamicTimeout, currentHelloInterval, TIMEOUT_MULTIPLIER);
}

void RoutingTableService::printRoutingTable() {
    ESP_LOGI(LM_TAG, "Current routing table:");

    routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        size_t position = 0;
        unsigned long currentTime = millis();

        do {
            RouteNode* node = routingTableList->getCurrent();
            
            // Calculate time to live (time remaining before timeout)
            unsigned long timeLeft = (node->timeout > currentTime) ? 
                                      (node->timeout - currentTime) / 1000 : 0;

            ESP_LOGI(LM_TAG, "%d - Addr:0x%04X via:0x%04X hops:%d role:%d TTL:%lus SNR:%ddB", 
                position,
                node->networkNode.address,
                node->via,
                node->networkNode.metric,
                node->networkNode.role,
                timeLeft,
                node->receivedSNR);

            position++;
        } while (routingTableList->next());
    }
    
    size_t totalNodes = routingTableList->getLength();
    ESP_LOGI(LM_TAG, "Total nodes in routing table: %d", totalNodes);

    routingTableList->releaseInUse();
}

bool RoutingTableService::manageTimeoutRoutingTable() {
    // Check current Hello Mode - DO NOT remove nodes during Fast Discovery or Stabilizing modes
    // - Fast Discovery: Provisioning phase with high collision rate
    // - Stabilizing: Network is forming stable routes after provisioning
    // - Normal: Fully stable network - safe to remove inactive nodes
    uint8_t currentMode = LoraMesher::getInstance().getCurrentHelloMode();
    
    if (currentMode == HELLO_MODE_FAST_DISCOVERY) {
        ESP_LOGI(LM_TAG, "Skipping timeout check - in Fast Discovery Mode (provisioning)");
        ESP_LOGI(LM_TAG, "Node removal is disabled during network discovery to avoid premature cleanup");
        return false; // No nodes removed
    }
    
    if (currentMode == HELLO_MODE_STABILIZING) {
        ESP_LOGI(LM_TAG, "Skipping timeout check - in Stabilizing Mode (post-provisioning)");
        ESP_LOGI(LM_TAG, "Node removal is disabled during network stabilization to allow routes to form");
        return false; // No nodes removed
    }
    
    ESP_LOGI(LM_TAG, "Checking routes timeout (Hello Mode: %d - Normal operation)", currentMode);

    bool nodeRemoved = false;

    routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            if (node->timeout < millis()) {
                ESP_LOGW(LM_TAG, "Route timeout %X via %X", node->networkNode.address, node->via);

                delete node;
                routingTableList->DeleteCurrent();
                nodeRemoved = true;
            }

        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();

    printRoutingTable();

    // Alert if routing table is empty (lost connection to all nodes)
    if (nodeRemoved) {
        size_t remainingNodes = routingTableSize();
        if (remainingNodes == 0) {
            ESP_LOGE(LM_TAG, "⚠️  CRITICAL: Routing table is now EMPTY - No nodes reachable!");
            ESP_LOGE(LM_TAG, "⚠️  Network is isolated. Waiting for Hello packets to rebuild routing table...");
        } else {
            ESP_LOGW(LM_TAG, "Routing table updated: %d node(s) remaining after timeout cleanup", remainingNodes);
        }
    }

    // Call callback if any node was removed and callback is registered (if not suspended)
    if (nodeRemoved && onRoutingTableChanged != nullptr && !callbackSuspended) {
        ESP_LOGI(LM_TAG, "Node(s) removed - triggering routing table save callback");
        onRoutingTableChanged();
    }

    return nodeRemoved;
}

uint8_t RoutingTableService::calculateMaximumMetricOfRoutingTable() {
    routingTableList->setInUse();

    uint8_t maximumMetricOfRoutingTable = 0;

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            if (node->networkNode.metric > maximumMetricOfRoutingTable)
                maximumMetricOfRoutingTable = node->networkNode.metric;

        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();

    return maximumMetricOfRoutingTable + 1;
}

void RoutingTableService::setRoutingTableChangedCallback(void (*callback)()) {
    onRoutingTableChanged = callback;
    ESP_LOGI(LM_TAG, "Routing table change callback registered");
}

void RoutingTableService::suspendCallback() {
    callbackSuspended = true;
    ESP_LOGD(LM_TAG, "Routing table callbacks suspended");
}

void RoutingTableService::resumeCallback() {
    callbackSuspended = false;
    ESP_LOGD(LM_TAG, "Routing table callbacks resumed");
}

// Static member initialization
LM_LinkedList<RouteNode>* RoutingTableService::routingTableList = new LM_LinkedList<RouteNode>();
void (*RoutingTableService::onRoutingTableChanged)() = nullptr;
bool RoutingTableService::callbackSuspended = false;