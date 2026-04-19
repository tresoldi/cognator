// Package eval implements B-cubed precision/recall/F for cognate
// clustering evaluation (Bagga & Baldwin 1998; Amigó et al. 2009).
// Keys in the predicted and truth maps are item IDs; values are
// opaque cluster labels. Per-item precision is the fraction of the
// predicted cluster that shares the true cluster; per-item recall
// is the fraction of the true cluster that shares the predicted
// cluster. Precision and recall are averaged across the items
// present in both maps.
package eval

// BCubed computes B-cubed precision, recall, and F1 given predicted
// and ground-truth cluster assignments. Only items present in both
// maps are scored.
func BCubed(pred, truth map[string]string) (precision, recall, f1 float64) {
	predMembers := membersBy(pred)
	truthMembers := membersBy(truth)

	var pSum, rSum float64
	var n int
	for id, pCluster := range pred {
		tCluster, ok := truth[id]
		if !ok {
			continue
		}
		pm := predMembers[pCluster]
		tm := truthMembers[tCluster]
		shared := intersectionSize(pm, tm)
		pSum += float64(shared) / float64(len(pm))
		rSum += float64(shared) / float64(len(tm))
		n++
	}
	if n == 0 {
		return 0, 0, 0
	}
	precision = pSum / float64(n)
	recall = rSum / float64(n)
	if precision+recall == 0 {
		return precision, recall, 0
	}
	f1 = 2 * precision * recall / (precision + recall)
	return
}

func membersBy(m map[string]string) map[string][]string {
	out := map[string][]string{}
	for id, c := range m {
		out[c] = append(out[c], id)
	}
	return out
}

func intersectionSize(a, b []string) int {
	set := make(map[string]struct{}, len(b))
	for _, x := range b {
		set[x] = struct{}{}
	}
	var n int
	for _, x := range a {
		if _, ok := set[x]; ok {
			n++
		}
	}
	return n
}
