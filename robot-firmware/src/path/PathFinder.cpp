/**
 * PathFinder.cpp
 * ==============
 * ESP32 로봇용 BFS 경로 탐색 구현.
 */

#include "PathFinder.h"

// 노드 이름 (이미지 번호 1~16)
// 인덱스: 0~3=a01~a04, 4~6=s05~s07, 7~9=r08~r10, 10~12=s11~s13, 13~15=r14~r16
static const char* NODE_NAMES[PATH_MAX_NODES] = {
    "a01", "a02", "a03", "a04",
    "s05", "s06", "s07",
    "r08", "r09", "r10",
    "s11", "s12", "s13",
    "r14", "r15", "r16"
};

// 방향: 0=N, 1=E, 2=S, 3=W

static void addGraphEdge(PathNode* nodes, int from, int to, int exitDir) {
    if (nodes[from].edgeCount < PATH_MAX_EDGES) {
        nodes[from].edges[nodes[from].edgeCount].targetIdx = to;
        nodes[from].edges[nodes[from].edgeCount].exitDir = exitDir;
        nodes[from].edgeCount++;
    }
}

PathFinder::PathFinder() : _initialized(false) {
}

void PathFinder::initGraph() {
    if (_initialized) return;

    for (int i = 0; i < PATH_MAX_NODES; i++) {
        _nodes[i].name = NODE_NAMES[i];
        _nodes[i].edgeCount = 0;
    }

    // 메인: 1-2-3-4 (a01-a02-a03-a04)
    addGraphEdge(_nodes, 0, 1, 1); addGraphEdge(_nodes, 1, 0, 3);
    addGraphEdge(_nodes, 1, 2, 1); addGraphEdge(_nodes, 2, 1, 3);
    addGraphEdge(_nodes, 2, 3, 1); addGraphEdge(_nodes, 3, 2, 3);

    // 2↔6 (a02↔s06)
    addGraphEdge(_nodes, 1, 5, 0); addGraphEdge(_nodes, 5, 1, 2);

    // 상부 5-6-7 (s05-s06-s07)
    addGraphEdge(_nodes, 4, 5, 1); addGraphEdge(_nodes, 5, 4, 3);
    addGraphEdge(_nodes, 5, 6, 1); addGraphEdge(_nodes, 6, 5, 3);

    // 5↔11, 6↔12, 7↔13 (s05↔s11, s06↔s12, s07↔s13)
    addGraphEdge(_nodes, 4, 10, 0); addGraphEdge(_nodes, 10, 4, 2);
    addGraphEdge(_nodes, 5, 11, 0); addGraphEdge(_nodes, 11, 5, 2);
    addGraphEdge(_nodes, 6, 12, 0); addGraphEdge(_nodes, 12, 6, 2);

    // 상부 11-12-13 (s11-s12-s13)
    addGraphEdge(_nodes, 10, 11, 1); addGraphEdge(_nodes, 11, 10, 3);
    addGraphEdge(_nodes, 11, 12, 1); addGraphEdge(_nodes, 12, 11, 3);

    // 3↔9 (a03↔r09) - a03은 r09만 연결
    addGraphEdge(_nodes, 2, 8, 2); addGraphEdge(_nodes, 8, 2, 0);

    // 하부 8-9-10 (r08-r09-r10)
    addGraphEdge(_nodes, 7, 8, 1); addGraphEdge(_nodes, 8, 7, 3);
    addGraphEdge(_nodes, 8, 9, 1); addGraphEdge(_nodes, 9, 8, 3);

    // 8↔14, 9↔15, 10↔16 (r08↔r14, r09↔r15, r10↔r16)
    addGraphEdge(_nodes, 7, 13, 2); addGraphEdge(_nodes, 13, 7, 0);
    addGraphEdge(_nodes, 8, 14, 2); addGraphEdge(_nodes, 14, 8, 0);
    addGraphEdge(_nodes, 9, 15, 2); addGraphEdge(_nodes, 15, 9, 0);

    // 하부 14-15-16 (r14-r15-r16)
    addGraphEdge(_nodes, 13, 14, 1); addGraphEdge(_nodes, 14, 13, 3);
    addGraphEdge(_nodes, 14, 15, 1); addGraphEdge(_nodes, 15, 14, 3);

    _initialized = true;
    Serial.println("[PathFinder] 그래프 초기화 완료");
}

char PathFinder::dirDiffToChar(int currentDir, int targetDir) const {
    int diff = (targetDir - currentDir + 4) % 4;
    switch (diff) {
        case 0: return 'S';  // 직진
        case 1: return 'R';  // 우회전
        case 2: return 'U';  // U턴
        case 3: return 'L';  // 좌회전
        default: return 'S';
    }
}

int PathFinder::calculatePath(int startIdx, int targetIdx, int startDir, char* outPath,
                              int* outNodeSeq, int maxNodes) {
    if (!_initialized) initGraph();
    if (outPath == nullptr) return -1;
    if (startIdx < 0 || startIdx >= PATH_MAX_NODES ||
        targetIdx < 0 || targetIdx >= PATH_MAX_NODES) {
        return -1;
    }
    if (startIdx == targetIdx) {
        outPath[0] = 'E';
        outPath[1] = '\0';
        if (outNodeSeq && maxNodes >= 2) {
            outNodeSeq[0] = startIdx;
            outNodeSeq[1] = targetIdx;
        }
        return 1;
    }

    // BFS: parent[i] = {prevNode, exitDir from prev}
    struct BfsState {
        int prevNode;
        int exitDir;
        bool visited;
    };
    BfsState state[PATH_MAX_NODES];
    for (int i = 0; i < PATH_MAX_NODES; i++) {
        state[i].visited = false;
        state[i].prevNode = -1;
    }

    // 간단한 큐 (인덱스만 저장)
    int queue[PATH_MAX_NODES];
    int qHead = 0, qTail = 0;
    queue[qTail++] = startIdx;
    state[startIdx].visited = true;

    while (qHead < qTail) {
        int cur = queue[qHead++];
        if (cur == targetIdx) break;

        for (int e = 0; e < _nodes[cur].edgeCount; e++) {
            int next = _nodes[cur].edges[e].targetIdx;
            int exitDir = _nodes[cur].edges[e].exitDir;
            if (!state[next].visited) {
                state[next].visited = true;
                state[next].prevNode = cur;
                state[next].exitDir = exitDir;
                queue[qTail++] = next;
            }
        }
    }

    if (!state[targetIdx].visited) {
        outPath[0] = '\0';
        return -1;
    }

    // 역추적하여 경로 구성 (target -> start)
    int pathDirs[PATH_MAX_NODES];
    int pathNodes[PATH_MAX_NODES];
    int pathLen = 0;
    int node = targetIdx;
    while (state[node].prevNode >= 0) {
        pathDirs[pathLen] = state[node].exitDir;
        pathNodes[pathLen] = node;
        pathLen++;
        node = state[node].prevNode;
    }
    pathNodes[pathLen] = startIdx;  // pathNodes[pathLen..0] = start, ..., target

    // 노드 시퀀스 출력 (start -> ... -> target)
    if (outNodeSeq && maxNodes > 0) {
        int seqLen = 0;
        for (int j = 0; j <= pathLen && seqLen < maxNodes; j++) {
            outNodeSeq[seqLen++] = pathNodes[pathLen - j];
        }
        if (seqLen < maxNodes) outNodeSeq[seqLen] = -1;
    }

    // 경로를 start -> target 순으로 변환
    // pathDirs[pathLen-1] = start에서 다음 노드로 나가는 방향
    // pathDirs[pathLen-2] = 두번째 노드에서 세번째로 나가는 방향, ...
    int currentDir = startDir;
    int outLen = 0;
    for (int i = pathLen - 1; i >= 0 && outLen < PATH_STRING_MAX - 2; i--) {
        int exitDir = pathDirs[i];
        char cmd = dirDiffToChar(currentDir, exitDir);
        outPath[outLen++] = cmd;
        currentDir = exitDir;  // 진입 후 바라보는 방향 = 나간 방향과 동일
    }
    outPath[outLen++] = 'E';
    outPath[outLen] = '\0';

    return outLen;
}

int PathFinder::nodeNameToIndex(const char* nodeName) const {
    if (nodeName == nullptr) return -1;
    for (int i = 0; i < PATH_MAX_NODES; i++) {
        if (strcmp(NODE_NAMES[i], nodeName) == 0) return i;
    }
    return -1;
}

const char* PathFinder::indexToNodeName(int idx) const {
    if (idx < 0 || idx >= PATH_MAX_NODES) return "";
    return NODE_NAMES[idx];
}
