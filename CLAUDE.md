# Tengram: facts about the target

- **One OS version.** Every phone that runs Tengram runs the same BlackBerry 10 as the test phone:
  10.3.3.3216 (read on bb10rootx). BB10 gets no further updates. Don't write code for other OS versions,
  check for them, or plan for a future one; what works on the test phone's OS works everywhere.
- **Every phone is rooted.** No Tengram user runs an unrooted phone, so a fix that needs root is a product
  route, not a lab hack. Don't build "without root" fallbacks.
