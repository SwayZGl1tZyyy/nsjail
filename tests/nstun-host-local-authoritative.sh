#!/bin/sh
set -eu

NSJAIL_BIN=${NSJAIL_BIN:-./nsjail}
LOCAL_IP=${LOCAL_IP:-192.0.2.10}
LOCAL_PORT=${LOCAL_PORT:-38080}
REMOTE_IP=${REMOTE_IP:-198.51.100.2}
REMOTE_PORT=${REMOTE_PORT:-38081}

suffix=$(( $$ % 100000 ))
dummy="njl${suffix}"
netns="nstun-remote-${suffix}"
veth_host="njh${suffix}"
veth_peer="njp${suffix}"

tmp=${TMPDIR:-/tmp}
cfg=$(mktemp "${tmp}/nstun-host-local.XXXXXX")
local_root=$(mktemp -d "${tmp}/nstun-host-local-www.XXXXXX")
remote_root=$(mktemp -d "${tmp}/nstun-remote-www.XXXXXX")
local_log="${tmp}/nstun-host-local-${suffix}.log"
remote_log="${tmp}/nstun-remote-${suffix}.log"

cleanup() {
	set +e
	if [ "${local_server_pid:-}" != "" ]; then
		kill "$local_server_pid" 2>/dev/null
		wait "$local_server_pid" 2>/dev/null
	fi
	ip netns pids "$netns" 2>/dev/null | xargs -r kill 2>/dev/null
	ip netns del "$netns" 2>/dev/null
	ip link del "$dummy" 2>/dev/null
	ip link del "$veth_host" 2>/dev/null
	rm -f "$cfg" "$local_log" "$remote_log"
	rm -rf "$local_root" "$remote_root"
}
trap cleanup EXIT INT TERM

[ "$(id -u)" -eq 0 ] || {
	echo "this test must run as root" >&2
	exit 1
}

ip link add "$dummy" type dummy
ip addr add "$LOCAL_IP/32" dev "$dummy"
ip link set "$dummy" up

ip netns add "$netns"
ip link add "$veth_host" type veth peer name "$veth_peer"
ip addr add 198.51.100.1/30 dev "$veth_host"
ip link set "$veth_host" up
ip link set "$veth_peer" netns "$netns"
ip netns exec "$netns" ip addr add "$REMOTE_IP/30" dev "$veth_peer"
ip netns exec "$netns" ip link set lo up
ip netns exec "$netns" ip link set "$veth_peer" up

printf 'host-local-canary\n' >"$local_root/canary"
printf 'remote-canary\n' >"$remote_root/canary"

python3 -m http.server "$LOCAL_PORT" --bind "$LOCAL_IP" \
	--directory "$local_root" >"$local_log" 2>&1 &
local_server_pid=$!

ip netns exec "$netns" \
	python3 -m http.server "$REMOTE_PORT" --bind "$REMOTE_IP" \
	--directory "$remote_root" >"$remote_log" 2>&1 &

sleep 0.2

cat >"$cfg" <<'EOF'
name: "nstun-host-local-test"
mode: ONCE
time_limit: 5

clone_newnet: true
clone_newuser: false
clone_newns: false
clone_newpid: false
clone_newipc: false
clone_newuts: false
clone_newcgroup: false
clone_newtime: false

user_net {
	backend: NSTUN
	ip4: "10.255.255.2"
	gw4: "10.255.255.1"
	ip6: ""
	gw6: ""
}
EOF

local_route=$(ip route get "$LOCAL_IP" | head -n1)
case "$local_route" in
	local\ *) ;;
	*)
		echo "expected a local route for $LOCAL_IP, got: $local_route" >&2
		exit 1
		;;
esac

remote_route=$(ip route get "$REMOTE_IP" | head -n1)
case "$remote_route" in
	local\ *)
		echo "expected a non-local route for $REMOTE_IP, got: $remote_route" >&2
		exit 1
		;;
esac

if "$NSJAIL_BIN" --quiet --config "$cfg" -- \
	/usr/bin/wget -q -T 2 -O - "http://${LOCAL_IP}:${LOCAL_PORT}/canary" \
	2>/dev/null | grep -qx 'host-local-canary'; then
	echo "host-local destination was reachable through nstun" >&2
	exit 1
fi

remote_result=$(
	"$NSJAIL_BIN" --quiet --config "$cfg" -- \
		/usr/bin/wget -q -T 2 -O - "http://${REMOTE_IP}:${REMOTE_PORT}/canary"
)

[ "$remote_result" = "remote-canary" ] || {
	echo "ordinary routed destination failed: $remote_result" >&2
	exit 1
}

echo "PASS: nstun blocks host-local routes and preserves ordinary routed egress"
