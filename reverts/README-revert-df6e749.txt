[PATCH] Revert commit df6e749be00e6791f87c3d05460f644906d87293

This patch reverts the large accidental paste/duplication introduced by commit
`df6e749be00e6791f87c3d05460f644906d87293` into Source/GUI/TapeScene.cpp
while preserving later commit 39b36a1874772371f79b9ba2ae52bcb38f2846cf.

The file on the branch `revert-df6e749` will be replaced with the clean state
from commit 502b78a264546c63091ecdf5c2a4bcac6c488e8c (the parent of the bad
commit), but we keep the newer commit 39b36a1 (it does not touch TapeScene.cpp)
so the overall history after merging this branch will have the revert followed
by the later legitimate commit.

This is an automated revert prepared by Copilot.
