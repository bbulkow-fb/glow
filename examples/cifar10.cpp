#include "noether/Image.h"
#include "noether/Network.h"
#include "noether/Nodes.h"
#include "noether/Support.h"
#include "noether/Tensor.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

using namespace noether;

NodeBase *createSimpleNet(Network &N, NodeBase *input, NodeBase *expected) {
  auto *CV0 = N.createConvNode(input, 16, 5, 1, 2);
  auto *RL0 = N.createRELUNode(CV0);
  auto *MP0 = N.createMaxPoolNode(RL0, MaxPoolNode::OpKind::kMax, 2, 2, 0);

  auto *CV1 = N.createConvNode(MP0, 20, 5, 1, 2);
  auto *RL1 = N.createRELUNode(CV1);
  auto *MP1 = N.createMaxPoolNode(RL1, MaxPoolNode::OpKind::kMax, 2, 2, 0);

  auto *CV2 = N.createConvNode(MP1, 20, 5, 1, 2);
  auto *RL2 = N.createRELUNode(CV2);
  auto *MP2 = N.createMaxPoolNode(RL2, MaxPoolNode::OpKind::kMax, 2, 2, 0);

  auto *FCL1 = N.createFullyConnectedNode(MP2, 10);
  auto *RL3 = N.createRELUNode(FCL1);
  auto *SM = N.createSoftMaxNode(RL3, expected);
  return SM;
}

/// The CIFAR file format is structured as one byte label in the range 0..9.
/// The label is followed by an image: 32 x 32 pixels, in RGB format. Each
/// color is 1 byte. The first 1024 red bytes are followed by 1024 of green
/// and blue. Each 1024 byte color slice is organized in row-major format.
/// The database contains 10000 images.
/// Size: (1 + (32 * 32 * 3)) * 10000 = 30730000.
const size_t cifarImageSize = 1 + (32 * 32 * 3);
const size_t cifarNumImages = 10000;

/// This test classifies digits from the CIFAR labeled dataset.
/// Details: http://www.cs.toronto.edu/~kriz/cifar.html
/// Dataset: http://www.cs.toronto.edu/~kriz/cifar-10-binary.tar.gz
void testCIFAR10() {
  (void)cifarImageSize;
  const char *textualLabels[] = {"airplane", "automobile", "bird", "cat",
                                 "deer",     "dog",        "frog", "horse",
                                 "ship",     "truck"};

  std::ifstream dbInput("cifar-10-batches-bin/data_batch_1.bin",
                        std::ios::binary);

  std::cout << "Loading the CIFAR-10 database.\n";

  /// Load the CIFAR database into a 4d tensor.
  Tensor images(ElemKind::FloatTy, {cifarNumImages, 32, 32, 3});
  Tensor labels(ElemKind::IndexTy, {cifarNumImages, 1});
  size_t idx = 0;

  auto labelsH = labels.getHandle<size_t>();
  auto imagesH = images.getHandle<FloatTy>();
  for (unsigned w = 0; w < cifarNumImages; w++) {
    labelsH.at({w, 0}) = static_cast<uint8_t>(dbInput.get());
    idx++;

    for (unsigned z = 0; z < 3; z++) {
      for (unsigned y = 0; y < 32; y++) {
        for (unsigned x = 0; x < 32; x++) {
          imagesH.at({w, x, y, z}) =
              FloatTy(static_cast<uint8_t>(dbInput.get())) / 255.0;
          idx++;
        }
      }
    }
  }
  assert(idx == cifarImageSize * cifarNumImages && "Invalid input file");

  // Construct the network:
  Network N;
  N.getConfig().learningRate = 0.001;
  N.getConfig().momentum = 0.9;
  N.getConfig().L2Decay = 0.0001;

  unsigned minibatchSize = 8;

  // Create the input layer:
  auto *A = N.createVariable({minibatchSize, 32, 32, 3}, ElemKind::FloatTy);
  auto *E = N.createVariable({minibatchSize, 1}, ElemKind::IndexTy);

  // Create the rest of the network.
  NodeBase *SM = createSimpleNet(N, A, E);

  // Report progress every this number of training iterations.
  int reportRate = 256;

  std::cout << "Training.\n";

  for (int iter = 0; iter < 100000; iter++) {
    std::cout << "Training - iteration #" << iter << "\n";
    TimerGuard reportTime(reportRate * minibatchSize);

    // Bind the images tensor to the input array A, and the labels tensor
    // to the softmax node SM.
    N.train(SM, reportRate, {A, E}, {&images, &labels});

    unsigned score = 0;

    for (int i = 0; i < 100 / minibatchSize; i++) {
      Tensor sample(ElemKind::FloatTy, {minibatchSize, 3, 32, 32});
      sample.copyConsecutiveSlices(&images, minibatchSize * i);
      auto *res = N.infer(SM, {A}, {&sample});

      for (int iter = 0; iter < minibatchSize; iter++) {
        auto T = res->getHandle<FloatTy>().extractSlice(iter);
        size_t guess = T.getHandle<FloatTy>().maxArg();
        size_t correct = labelsH.at({minibatchSize * i + iter, 0});
        score += guess == correct;

        if ((iter < 10) && i == 0) {
          // T.getHandle<FloatTy>().dump("softmax: "," ");
          std::cout << iter << ") Expected : " << textualLabels[correct]
                    << " got " << textualLabels[guess] << "\n";
        }
      }
    }

    std::cout << "Batch #" << iter << " score: " << score << "%\n";
  }
}

int main() {
  testCIFAR10();

  return 0;
}
