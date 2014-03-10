#include <zkutil/ZooKeeper.h>
#include <boost/make_shared.hpp>


#define CHECKED(x) { ReturnCode::type code = x; if (code != ReturnCode::Ok) throw KeeperException(code); }

namespace zkutil
{

typedef std::promise<WatchEventInfo> WatchPromise;

struct WatchWithPromise : public zk::Watch
{
	WatchPromise promise;

	void process(WatchEvent::type event, SessionState::type state, const std::string & path)
	{
		promise.set_value(WatchEventInfo(event, state, path));
	}
};

typedef boost::shared_ptr<zk::Watch> WatchPtr;
typedef boost::shared_ptr<WatchWithPromise> WatchWithPromisePtr;

static WatchPtr watchForFuture(WatchFuture * future)
{
	if (!future)
		return nullptr;
	WatchWithPromisePtr res = boost::make_shared<WatchWithPromise>();
	*future = res->promise.get_future();
	return res;
}

struct StateWatch : public zk::Watch
{
	ZooKeeper * owner;

	StateWatch(ZooKeeper * owner_) : owner(owner_) {}

	void process(WatchEvent::type event, SessionState::type state, const std::string & path)
	{
		owner->stateChanged(event, state, path);
	}
};

ZooKeeper::ZooKeeper(const std::string & hosts, int32_t sessionTimeoutMs, WatchFunction * watch_)
	: state_watch(watch_)
{
	CHECKED(impl.init(hosts, sessionTimeoutMs, boost::make_shared<StateWatch>(this)));

	ACL perm;
	perm.getid().getscheme() = "world";
	perm.getid().getid() = "anyone";
	perm.setperms(zk::Permission::All);
	default_acl.push_back(perm);
}

void ZooKeeper::stateChanged(WatchEvent::type event, SessionState::type state, const std::string & path)
{
	session_state = state;
	if (state_watch)
		(*state_watch)(event, state, path);
}

bool ZooKeeper::disconnected()
{
	return session_state == SessionState::Expired || session_state == SessionState::AuthFailed;
}

void ZooKeeper::setDefaultACL(ACLs & acl)
{
	default_acl = acl;
}

ACLs ZooKeeper::getDefaultACL()
{
	return default_acl;
}

Strings ZooKeeper::getChildren(
	const std::string & path, Stat * stat, WatchFuture * watch)
{
	Stat s;
	Strings res;
	CHECKED(impl.getChildren(path, watchForFuture(watch), res, s));
	if (stat)
		*stat = s;
	return res;
}

bool ZooKeeper::tryGetChildren(const std::string & path, Strings & res,
								Stat * stat, WatchFuture * watch)
{
	Stat s;
	ReturnCode::type code = impl.getChildren(path, watchForFuture(watch), res, s);
	if (!(	code == ReturnCode::Ok ||
			code == ReturnCode::NoNode))
		throw KeeperException(code);
	if (code == ReturnCode::NoNode)
		return false;
	if (stat)
		*stat = s;
	return true;
}

std::string ZooKeeper::create(const std::string & path, const std::string & data, CreateMode::type mode)
{
	std::string res;
	CHECKED(impl.create(path, data, default_acl, mode, res));
	return res;
}

ReturnCode::type ZooKeeper::tryCreate(const std::string & path, const std::string & data, CreateMode::type mode, std::string & pathCreated)
{
	ReturnCode::type code = impl.create(path, data, default_acl, mode, pathCreated);
	if (!(	code == ReturnCode::Ok ||
			code == ReturnCode::NoNode ||
			code == ReturnCode::NodeExists ||
			code == ReturnCode::NoChildrenForEphemerals))
		throw KeeperException(code);
	return code;
}

void ZooKeeper::remove(const std::string & path, int32_t version)
{
	CHECKED(impl.remove(path, version));
}

ReturnCode::type ZooKeeper::tryRemove(const std::string & path, int32_t version)
{
	ReturnCode::type code = impl.remove(path, version);
	if (!(	code == ReturnCode::Ok ||
			code == ReturnCode::NoNode ||
			code == ReturnCode::BadVersion ||
			code == ReturnCode::NotEmpty))
		throw KeeperException(code);
	return code;
}

bool ZooKeeper::exists(const std::string & path, Stat * stat, WatchFuture * watch)
{
	Stat s;
	ReturnCode::type code = impl.exists(path, watchForFuture(watch), s);
	if (!(	code == ReturnCode::Ok ||
			code == ReturnCode::NoNode))
		throw KeeperException(code);
	if (code == ReturnCode::NoNode)
		return false;
	if (stat)
		*stat = s;
	return true;
}

std::string ZooKeeper::get(const std::string & path, Stat * stat, WatchFuture * watch)
{
	std::string res;
	Stat s;
	CHECKED(impl.get(path, watchForFuture(watch), res, s));
	if (stat)
		*stat = s;
	return res;
}

bool ZooKeeper::tryGet(const std::string & path, std::string & res, Stat * stat, WatchFuture * watch)
{
	Stat s;
	ReturnCode::type code = impl.get(path, watchForFuture(watch), res, s);
	if (!(	code == ReturnCode::Ok ||
			code == ReturnCode::NoNode))
		throw KeeperException(code);
	if (code == ReturnCode::NoNode)
		return false;
	if (stat)
		*stat = s;
	return true;
}

void ZooKeeper::set(const std::string & path, const std::string & data, int32_t version, Stat * stat)
{
	Stat s;
	CHECKED(impl.set(path, data, version, s));
	if (stat)
		*stat = s;
}

ReturnCode::type ZooKeeper::trySet(const std::string & path, const std::string & data,
									int32_t version, Stat * stat)
{
	Stat s;
	ReturnCode::type code = impl.set(path, data, version, s);
	if (!(	code == ReturnCode::Ok ||
			code == ReturnCode::NoNode ||
			code == ReturnCode::BadVersion))
		throw KeeperException(code);
	if (stat)
		*stat = s;
	return code;
}

OpResultsPtr ZooKeeper::multi(const Ops & ops)
{
	OpResultsPtr res = std::make_shared<OpResults>();
	CHECKED(impl.multi(ops, *res));
	for (size_t i = 0; i < res->size(); ++i)
	{
		if ((*res)[i].getReturnCode() != ReturnCode::Ok)
			throw KeeperException((*res)[i].getReturnCode());
	}
	return res;
}

OpResultsPtr ZooKeeper::tryMulti(const Ops & ops)
{
	OpResultsPtr res = std::make_shared<OpResults>();
	CHECKED(impl.multi(ops, *res));
	for (size_t i = 0; i < res->size(); ++i)
	{
		ReturnCode::type code = (*res)[i].getReturnCode();

		if (code != ReturnCode::Ok)
		{
			bool ok = false;
			switch (ops[i].getType())
			{
				case zk::OpCode::Create:
					ok |= code == ReturnCode::NoNode;
					ok |= code == ReturnCode::NodeExists;
					ok |= code == ReturnCode::NoChildrenForEphemerals;
					break;
				case zk::OpCode::Remove:
					ok |= code == ReturnCode::NoNode;
					ok |= code == ReturnCode::BadVersion;
					ok |= code == ReturnCode::NotEmpty;
					break;
				case zk::OpCode::SetData:
				case zk::OpCode::Check:
					ok |= code == ReturnCode::NoNode;
					ok |= code == ReturnCode::BadVersion;
					break;
				default:
					throw KeeperException("Unexpected op type");
			}

			if (!ok)
				throw KeeperException(code);
		}
	}
	return res;
}

void ZooKeeper::close()
{
	CHECKED(impl.close());
}

}