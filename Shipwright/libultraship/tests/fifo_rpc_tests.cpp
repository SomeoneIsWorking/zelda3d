#include <gtest/gtest.h>
#include <libultraship/bridge/fifo_rpc.h>

#include <string>

TEST(FifoRpc, ParsesTaggedAndLegacyRequests) {
    char id[ZELDA3D_FIFO_RPC_ID_LENGTH + 1];
    const char* command = nullptr;

    EXPECT_EQ(Zelda3DFifoRpc_ParseRequest("ping", id, &command), 0);
    EXPECT_STREQ(command, "ping");
    EXPECT_STREQ(id, "");

    EXPECT_EQ(Zelda3DFifoRpc_ParseRequest("@0123456789abcdef actors 3", id, &command), 1);
    EXPECT_STREQ(id, "0123456789abcdef");
    EXPECT_STREQ(command, "actors 3");
}

TEST(FifoRpc, RejectsMalformedTaggedRequests) {
    char id[ZELDA3D_FIFO_RPC_ID_LENGTH + 1];
    const char* command = nullptr;
    EXPECT_EQ(Zelda3DFifoRpc_ParseRequest("@0123456789abcde ping", id, &command), -1);
    EXPECT_STREQ(id, "");
    EXPECT_EQ(Zelda3DFifoRpc_ParseRequest("@0123456789abcdeG ping", id, &command), -1);
    EXPECT_STREQ(id, "");
    EXPECT_EQ(Zelda3DFifoRpc_ParseRequest("@0123456789abcdef_ping", id, &command), -1);
    EXPECT_STREQ(id, "");
}

TEST(FifoRpc, FormatsTypedDataAndTerminatorFrames) {
    char output[128];
    Zelda3DFifoRpc_FormatData(output, sizeof(output), "0123456789abcdef", "ok stick 0 72");
    EXPECT_STREQ(output, "@0123456789abcdef D ok stick 0 72");
    Zelda3DFifoRpc_FormatEnd(output, sizeof(output), "0123456789abcdef", 3);
    EXPECT_STREQ(output, "@0123456789abcdef E 3");
}
